// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - FaceLab. See LICENSE.

#include "Morph.h"
#include "FamilyLoad.h"
#include "Families.h"
#include "Lab.h"
#include "TriFile.h"

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <memory>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <map>

namespace FaceLab::Morph
{
	namespace
	{
		using Deltas = std::vector<float>;  // 3 floats per vertex

		struct TriSet
		{
			int                          nv{ 0 };
			std::string                  path;
std::vector<float>           base;
			mutable std::vector<float>   mouthMask;  // 0..1 per vertex: how much the lips / mouth region moves for the phoneme morphs (oral mode)
			mutable bool                 maskDone{ false };  // the file's base vertices (3 floats per vertex): used to tell apart face parts that have the same vertex count
			std::map<std::string, Deltas> morphs;  // "BigAah" -> offsets (the EXPR_ prefix is dropped)
		};

		const char* kRoot = "Data/Meshes/actors/character/facegenmorphs/morphs/";  // the expression files indexed for face parts that are in no family list

		// ---- head families (Families.h): the kind of head an actor has decides which morph files drive its face ----
		std::string ResolveMesh(const std::string& rel)
		{
			std::error_code   ec;
			const std::string p = "Data/Meshes/" + rel;
			return std::filesystem::exists(p, ec) ? p : std::string{};
		}

		TriSet ToTriSet(Fam::Part&& p)
		{
			TriSet t;
			t.nv = p.nv;
			t.path = p.files.empty() ? std::string{} : p.files.front();
			t.base = std::move(p.base);
			for (auto& [name, dl] : p.morphs) {
				t.morphs.emplace(name, std::move(dl));
			}
			return t;
		}

		struct FamRt
		{
			int                                       headNv{ 0 };  // vertex count of the family's head file (0 = that file is not installed)
			std::vector<int>                          nvs;          // vertex counts of every installed file of the family
			std::atomic<int>                          state{ 0 };   // 0 not loaded, 1 loading in the background, 2 ready
			std::vector<std::unique_ptr<TriSet>>      parts;
			std::map<int, std::vector<const TriSet*>> byCount;      // vertex count -> the parts with that many vertices (left / right eye ... share a count)
		};
		std::vector<std::unique_ptr<FamRt>> g_fam;
		std::once_flag                      g_famOnce;

		void InitFamilies()
		{
			std::call_once(g_famOnce, []() {
				const auto& all = Fam::All();
				for (const auto& def : all) {
					auto fr = std::make_unique<FamRt>();
					for (const auto& h : Fam::Headers(def, ResolveMesh)) {
						fr->nvs.push_back(h.nv);
					}
					if (!def.files.empty()) {
						const std::string full = ResolveMesh(def.files.front());
						int               nv = 0;
						if (!full.empty() && Tri::ReadVertexCount(full, nv)) {
							fr->headNv = nv;
							// CoTR's high poly head (12206 vertices) is not supported: it has little use and was never made to work. Its head file reads as 'not installed',
							// so such a head is simply not driven (the low poly CoTR head, 3127 vertices, is).
							if (std::string_view(def.name).starts_with("CoTR") && nv > 5000) {
								logger::info("Morph: head family '{}': the head file has {} vertices (CoTR high poly): not supported, ignored", def.name, nv);
								fr->headNv = 0;
							}
						}
					}
					logger::info("Morph: head family '{}': head file {} vertices, {} morph file(s) installed", def.name, fr->headNv, fr->nvs.size());
					g_fam.push_back(std::move(fr));
				}
			});
		}

		void StartLoad(int fi)
		{
			InitFamilies();
			auto& fr = *g_fam[static_cast<std::size_t>(fi)];
			int   expected = 0;
			if (!fr.state.compare_exchange_strong(expected, 1)) {
				return;
			}
			std::thread([fi]() {
				auto&                    fr = *g_fam[static_cast<std::size_t>(fi)];
				std::vector<std::string> log;
				auto                     parts = Fam::Load(Fam::All()[static_cast<std::size_t>(fi)], ResolveMesh, &log);
				for (auto& p : parts) {
					fr.parts.push_back(std::make_unique<TriSet>(ToTriSet(std::move(p))));
				}
				for (const auto& t : fr.parts) {
					fr.byCount[t->nv].push_back(t.get());
				}
				for (const auto& l : log) {
					logger::info("Morph: {}", l);
				}
				logger::info("Morph: head family '{}' loaded: {} face part(s)", Fam::All()[static_cast<std::size_t>(fi)].name, fr.parts.size());
				fr.state = 2;
			}).detach();
		}

		// Which family fits an actor of this sex, from the vertex counts of its face shapes. The head's count is the strong sign; without it at least two different
		// face parts must match (one lone match can be a coincidence). -1 = none. With equal scores the family listed first wins.
		int FamilyOf(bool female, const std::vector<int>& counts)
		{
			InitFamilies();
			const auto& all = Fam::All();
			int         best = -1, bestScore = 0;
			for (std::size_t i = 0; i < all.size(); ++i) {
				if (all[i].female != female) {
					continue;
				}
				const auto&   fr = *g_fam[i];
				bool          headHit = false;
				std::set<int> hit;
				for (const int n : counts) {
					if (fr.headNv > 0 && n == fr.headNv) {
						headHit = true;
					} else if (std::ranges::find(fr.nvs, n) != fr.nvs.end()) {
						hit.insert(n);
					}
				}
				const int score = headHit ? 100 + static_cast<int>(hit.size()) : (hit.size() >= 2 ? static_cast<int>(hit.size()) : 0);
				if (score > bestScore) {
					bestScore = score;
					best = static_cast<int>(i);
				}
			}
			return best;
		}

		// the morph of this name in a part: the exact name, else the nearest alias (a head that has no 'Oh' of its own uses OohQ)
		const Deltas* FindMorph(const TriSet& s, const std::string& name)
		{
			if (const auto it = s.morphs.find(name); it != s.morphs.end()) {
				return &it->second;
			}
			for (const char* al : Fam::Aliases(name)) {
				if (const auto it = s.morphs.find(al); it != s.morphs.end()) {
					return &it->second;
				}
			}
			return nullptr;
		}

		// a face part of the expression index outside the family lists (beards ...), read as a part of its own
		bool LoadDyn(const std::string& path, TriSet& out)
		{
			Fam::Part   p;
			std::string err;
			if (!Fam::LoadFile(path, p, err)) {
				logger::warn("Morph: cannot use {} ({})", path, err);
				return false;
			}
			out = ToTriSet(std::move(p));
			logger::info("Morph: {} -> {} vertices, {} morphs", path, out.nv, out.morphs.size());
			return true;
		}


		// ---- face parts that are not in the fixed lists (beards, lashes ...) ----
		// Every .tri under the morphs folder whose name has "expr" in it is indexed by vertex count (header only, once, in the background). A shape that is not in the
		// fixed list is matched to the candidate file with its vertex count whose base vertices lie closest to the shape's own.
		struct IdxEntry
		{
			std::string path;
			int         nv;
		};
		std::vector<IdxEntry>                          g_idx;
		std::atomic<int>                               g_idxState{ 0 };  // 0 not started, 1 building, 2 ready
		std::mutex                                     g_dynMx;
		std::map<int, std::vector<std::unique_ptr<TriSet>>> g_dyn;

		void BuildIndex()
		{
			int expected = 0;
			if (!g_idxState.compare_exchange_strong(expected, 1)) {
				return;
			}
			std::thread([]() {
				namespace fs = std::filesystem;
				std::error_code ec;
				std::vector<IdxEntry> out;
				for (fs::recursive_directory_iterator it(kRoot, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
					if (!it->is_regular_file(ec)) {
						continue;
					}
					std::string name = it->path().filename().string();
					std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					if (!name.ends_with(".tri") || name.find("expr") == std::string::npos) {
						continue;
					}
					std::ifstream f(it->path(), std::ios::binary);
					char          h[12]{};
					if (!f.read(h, 12) || std::memcmp(h, "FRTRI003", 8) != 0) {
						continue;
					}
					std::int32_t nv = 0;
					std::memcpy(&nv, h + 8, 4);
					if (nv > 0) {
						out.push_back({ it->path().generic_string(), nv });
					}
				}
				{
					std::lock_guard lk(g_dynMx);
					g_idx = std::move(out);
				}
				logger::info("Morph: indexed {} expression .tri files for face parts outside the fixed list", g_idx.size());
				g_idxState = 2;
			}).detach();
		}

		// d = the shape's live vertex data (4 floats per vertex). pending = the index is not ready yet (ask again later).
		const TriSet* ResolveDynamic(int nv, const float* d, const char* shapeName, bool& pending)
		{
			pending = false;
			if (g_idxState.load() != 2) {
				pending = true;
				BuildIndex();
				return nullptr;
			}
			std::lock_guard lk(g_dynMx);
			static std::set<int> loaded;
			auto&                vec = g_dyn[nv];
			if (loaded.insert(nv).second) {
				for (const auto& e : g_idx) {
					if (e.nv != nv) {
						continue;
					}
					auto t = std::make_unique<TriSet>();
					if (LoadDyn(e.path, *t) && t->nv == nv && t->base.size() == static_cast<std::size_t>(nv) * 3) {
						vec.push_back(std::move(t));
					}
				}
			}
			const TriSet* best = nullptr;
			double        bestDist = 1e30;
			// the file's vertices and the live ones can sit in different origins (a beard is ~120 units off): compare the shapes around their own centres
			double cx = 0.0, cy = 0.0, cz = 0.0;
			for (int v = 0; v < nv; ++v) {
				cx += d[v * 4];
				cy += d[v * 4 + 1];
				cz += d[v * 4 + 2];
			}
			cx /= nv;
			cy /= nv;
			cz /= nv;
			for (const auto& c : vec) {
				double bx = 0.0, by = 0.0, bz = 0.0;
				for (int v = 0; v < nv; ++v) {
					bx += c->base[static_cast<std::size_t>(v) * 3];
					by += c->base[static_cast<std::size_t>(v) * 3 + 1];
					bz += c->base[static_cast<std::size_t>(v) * 3 + 2];
				}
				bx /= nv;
				by /= nv;
				bz /= nv;
				double sum = 0.0;
				for (int v = 0; v < nv; ++v) {
					const double dx = (c->base[static_cast<std::size_t>(v) * 3] - bx) - (d[v * 4] - cx), dy = (c->base[static_cast<std::size_t>(v) * 3 + 1] - by) - (d[v * 4 + 1] - cy),
					             dz = (c->base[static_cast<std::size_t>(v) * 3 + 2] - bz) - (d[v * 4 + 2] - cz);
					sum += std::sqrt(dx * dx + dy * dy + dz * dz);
				}
				sum /= nv;
				if (sum < bestDist) {
					bestDist = sum;
					best = c.get();
				}
			}
			if (best && bestDist < 6.0) {
				logger::info("Morph: face part '{}' ({} verts) uses {} (mean distance of the base vertices {:.2f})", shapeName, nv, best->path, bestDist);
				return best;
			}
			static std::set<std::string> told;
			if (told.size() < 40 && told.insert(std::format("{}:{}", shapeName, nv)).second) {
				logger::info("Morph: face part '{}' ({} verts): no expression file fits ({} candidate(s), closest {:.2f})", shapeName, nv, vec.size(), best ? bestDist : -1.0);
			}
			return nullptr;
		}

		// of several parts with the same vertex count (left / right eye, left / right mask), the one whose shape is the live shape's (compared around their own centres)
		const TriSet* ClosestPart(const std::vector<const TriSet*>& cands, int nv, const float* d)
		{
			double cx = 0.0, cy = 0.0, cz = 0.0;
			for (int v = 0; v < nv; ++v) {
				cx += d[v * 4];
				cy += d[v * 4 + 1];
				cz += d[v * 4 + 2];
			}
			cx /= nv;
			cy /= nv;
			cz /= nv;
			const TriSet* best = nullptr;
			double        bestDist = 1e30;
			for (const TriSet* c : cands) {
				if (c->base.size() != static_cast<std::size_t>(nv) * 3) {
					continue;
				}
				double bx = 0.0, by = 0.0, bz = 0.0;
				for (int v = 0; v < nv; ++v) {
					bx += c->base[static_cast<std::size_t>(v) * 3];
					by += c->base[static_cast<std::size_t>(v) * 3 + 1];
					bz += c->base[static_cast<std::size_t>(v) * 3 + 2];
				}
				bx /= nv;
				by /= nv;
				bz /= nv;
				double sum = 0.0;
				for (int v = 0; v < nv; ++v) {
					const double dx = (c->base[static_cast<std::size_t>(v) * 3] - bx) - (d[v * 4] - cx), dy = (c->base[static_cast<std::size_t>(v) * 3 + 1] - by) - (d[v * 4 + 1] - cy),
					             dz = (c->base[static_cast<std::size_t>(v) * 3 + 2] - bz) - (d[v * 4 + 2] - cz);
					sum += std::sqrt(dx * dx + dy * dy + dz * dz);
				}
				sum /= nv;
				if (sum < bestDist) {
					bestDist = sum;
					best = c;
				}
			}
			return best ? best : cands.front();
		}

		// the morph set of one shape: the actor's head family by vertex count, else the closest file in the expression index (beards, other face parts)
		// A family that is still loading (background thread) gives nothing yet: the face is applied on a later frame.
		const TriSet* SetFor(int fam, RE::BSDynamicTriShape* ds, int nv, const float* d)
		{
			static std::map<std::pair<const void*, const void*>, const TriSet*> cache;  // (shape, its vertex buffer): only touched under g_mx
			const auto key = std::make_pair(static_cast<const void*>(ds), static_cast<const void*>(d));
			if (fam >= 0) {
				auto&     fr = *g_fam[static_cast<std::size_t>(fam)];
				const int st = fr.state.load();
				if (st != 2) {
					if (st == 0) {
						StartLoad(fam);
					}
					return nullptr;
				}
				if (const auto it = fr.byCount.find(nv); it != fr.byCount.end()) {
					if (it->second.size() == 1) {
						return it->second.front();
					}
					if (const auto c = cache.find(key); c != cache.end()) {
						return c->second;
					}
					const TriSet* r = ClosestPart(it->second, nv, d);
					if (cache.size() > 4000) {
						cache.clear();
					}
					cache[key] = r;
					return r;
				}
			}
			if (const auto c = cache.find(key); c != cache.end()) {
				return c->second;
			}
			bool          pending = false;
			const TriSet* r = ResolveDynamic(nv, d, ds->name.c_str(), pending);
			if (!pending) {
				if (cache.size() > 4000) {
					cache.clear();
				}
				cache[key] = r;
			}
			return r;
		}

		// Dev aid for the 'one eye blinks / flickers' report (switch off before release): left / right eye shape statistics and one-sided change detection.
		constexpr bool kEyeWatch = false;
		struct SideStat
		{
			std::atomic<std::uint64_t> passes{ 0 }, rewritten{ 0 }, resets{ 0 }, changed{ 0 };
		};
		SideStat                   g_side[2];  // 0 = left eye shapes, 1 = right eye shapes
		std::atomic<std::uint64_t> g_oneSided{ 0 }, g_weightAsym{ 0 };
		std::atomic<int>           g_watchLines{ 0 };  // detail lines are capped per session
		int EyeSide(const char* a_name)
		{
			std::string n = a_name ? a_name : "";
			for (auto& c : n) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			if (n.find("eye") == std::string::npos) {
				return -1;
			}
			if (n.find("left") != std::string::npos) {
				return 0;
			}
			if (n.find("right") != std::string::npos) {
				return 1;
			}
			return -1;
		}
		std::atomic<std::uint64_t> g_passAll{ 0 }, g_passSkipped{ 0 };  // shape passes in Apply, and how many of them needed no rebuild (for the perf line)

		struct ShapeState
		{
			std::vector<float> applied;  // the offset (3 floats per vertex) that is inside the live mesh and the base arrays right now
			std::vector<float> lastOut;  // the live positions right after our last write
			void*              dyn{ nullptr };  // the live vertex buffer this record was made for: a different one means the shape was rebuilt (or its address reused)
			float*             fod{ nullptr };  // base vertices: the shape's 'FOD' extra data, 3 floats per vertex
			float*             fmd{ nullptr };  // the second copy found under 'FMD'
			bool               looked{ false };
			bool               fodWorks{ true };  // the engine builds the face from the patched base (learned at its first rewrite)
			int                verdicts{ 0 };
			// What 'applied' was built from. When the next pass would build it from exactly the same inputs, the result is identical, so the (expensive) rebuild is skipped.
			std::vector<std::pair<std::string, float>> key;
			const void*        keySet{ nullptr };
			bool               keyOral{ false };
			float              keyPoke{ 0.0f };
			bool               haveKey{ false };
		};

		struct ActorState
		{
			bool                                     female{ true };
			std::map<std::string, float>             target;
			std::map<std::string, float>             cur;
			float                                    fade{ 0.0f };
			bool                                     active{ false };
			bool                                     releasing{ false };
std::map<RE::BSDynamicTriShape*, ShapeState> shapes;
			bool                                     wAsymOn{ false };  // eye watch: unequal left / right eye weights right now
			std::chrono::steady_clock::time_point    wLastLog{};
			// oral mode: OStim's own mouth opening, remembered so a face rebuild (it resets the mouth to shut for a second or two) can be bridged
			float                                    hiNat{ 0.0f };
			std::chrono::steady_clock::time_point    hiAt{};
			bool                                     rebuilt{ false };
			float                                    bridgeVal{ 0.0f };
			std::chrono::steady_clock::time_point    bridgeAt{};
			float                                    hiVec[16]{};      // the 16 MFG phonemes drawn when the mouth was at its most open (Aah, BigAah, BMP ... W)
			float                                    bridgeVec[16]{};  // the same, frozen for the bridge over a rebuild
				// the gaze layer: eye channels added ON TOP of whatever face is showing (and shown with no face at all). Its own weights and fade, so a face's expression and
				// a gaze never fight; cur (the face) stays untouched, which keeps EyeControl / MouthOpen / Tongue about the face alone.
				std::map<std::string, float>             gazeTarget;
				std::map<std::string, float>             gazeCur;
float                                    gazeFade{ 0.05f };
				RE::BSFaceGenNiNode*                     nodeA{ nullptr };  // her face nodes as Frame() last saw them: the face-update hook compares against these
				RE::BSFaceGenNiNode*                     nodeB{ nullptr };
			};

		// the per-shape measurement lines are for tracking a face problem down: they are written (and flushed to disk) at every scene start and every climax
		bool Chatty()
		{
			return GetSettings().debug;
		}

		std::atomic<float> g_poke{ 0.0f };
		std::atomic<bool> g_bump{ false };  // patch the FMD copy too: debug only, off. (It was true once and corrupted engine memory when the API path never switched it off.)
		std::atomic<int>  g_dbg{ 0 };  // how many more diagnostic lines to write

		std::mutex                           g_mx;
		std::map<RE::FormID, ActorState>     g_act;
		std::chrono::steady_clock::time_point g_last = std::chrono::steady_clock::now();

		bool Readable(const void* p, std::size_t n)
		{
			if (!p) {
				return false;
			}
			MEMORY_BASIC_INFORMATION mbi{};
			if (!::VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD | PAGE_READONLY))) {
				return false;
			}
			return static_cast<const char*>(mbi.BaseAddress) + mbi.RegionSize >= static_cast<const char*>(p) + n;
		}

		// FOD extra data: +0x18 -> float3 base vertices, +0x20 -> vertex count (found by probing a real head)
		float* FindFod(RE::BSDynamicTriShape* ds, std::size_t nv)
		{
			auto* e = ds->GetExtraData("FOD");
			if (!e || !Readable(e, 0x30)) {
				return nullptr;
			}
			auto*      arr = *reinterpret_cast<float**>(reinterpret_cast<char*>(e) + 0x18);
			const auto cnt = *reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(e) + 0x20);
			if (cnt != nv || !Readable(arr, nv * 12)) {
				return nullptr;
			}
			return arr;
		}

		// FMD extra data: +0x18 -> object, +0x10 -> object, +0x48 -> float3 vertices
		float* FindFmd(RE::BSDynamicTriShape* ds, std::size_t nv)
		{
			auto* e = ds->GetExtraData("FMD");
			if (!e || !Readable(e, 0x20)) {
				return nullptr;
			}
			const auto q0 = *reinterpret_cast<std::uintptr_t*>(reinterpret_cast<char*>(e) + 0x18);
			if (!Readable(reinterpret_cast<void*>(q0), 0x18)) {
				return nullptr;
			}
			const auto q1 = *reinterpret_cast<std::uintptr_t*>(q0 + 0x10);
			if (!Readable(reinterpret_cast<void*>(q1), 0x50)) {
				return nullptr;
			}
			auto* arr = *reinterpret_cast<float**>(q1 + 0x48);
			return Readable(arr, nv * 12) ? arr : nullptr;
		}

		// move the weights toward their targets; true while any of them is not zero (the face's or the gaze's)
		bool Ease(ActorState& as, float dt)
		{
			const float k = as.fade <= 0.001f ? 1.0f : std::min(1.0f, dt / as.fade);
			bool        anyNonZero = false;
			for (auto& [name, t] : as.target) {
				float& c = as.cur[name];
				c += (t - c) * k;
				if (std::fabs(t - c) < 0.002f) {
					c = t;
				}
				anyNonZero |= std::fabs(c) > 0.0005f;
			}
			const float kg = as.gazeFade <= 0.001f ? 1.0f : std::min(1.0f, dt / as.gazeFade);
			for (auto& [name, t] : as.gazeTarget) {
				float& c = as.gazeCur[name];
				c += (t - c) * kg;
				if (std::fabs(t - c) < 0.002f) {
					c = t;
				}
				anyNonZero |= std::fabs(c) > 0.0005f;
			}
			return anyNonZero;
		}

		bool AnyNonZero(const ActorState& as)
		{
			for (const auto& [name, c] : as.cur) {
				if (std::fabs(c) > 0.0005f) {
					return true;
				}
			}
			for (const auto& [name, c] : as.gazeCur) {
				if (std::fabs(c) > 0.0005f) {
					return true;
				}
			}
			return false;
		}

		// what is drawn: the face plus the gaze, channel by channel (a weight never goes past 3.0)
		std::map<std::string, float> Merged(const ActorState& as)
		{
			if (as.gazeCur.empty()) {
				return as.cur;
			}
			std::map<std::string, float> out = as.cur;
			for (const auto& [name, c] : as.gazeCur) {
				float& v = out[name];
				v = std::clamp(v + c, -3.0f, 3.0f);
			}
			return out;
		}

		// the actor has two face nodes: GetFaceNode() and GetFaceNodeSkinned() (the one that is drawn, skinned to the head bones). Shape pointers from both.
		std::vector<RE::BSDynamicTriShape*> FaceShapes(RE::Actor* a)
		{
			std::vector<RE::BSDynamicTriShape*> out;
			static std::atomic<int> s_logged{ 0 };
			RE::BSFaceGenNiNode*    nodes[2] = { a->GetFaceNode(), a->GetFaceNodeSkinned() };
			if (s_logged.load() < 6) {
				++s_logged;
				if (Chatty()) logger::info("face nodes of {}: GetFaceNode={:p} GetFaceNodeSkinned={:p} same={}", a->GetDisplayFullName(), static_cast<void*>(nodes[0]), static_cast<void*>(nodes[1]), nodes[0] == nodes[1]);
			}
			for (int n = 0; n < 2; ++n) {
				if (!nodes[n] || (n == 1 && nodes[1] == nodes[0])) {
					continue;
				}
				for (auto& c : nodes[n]->GetChildren()) {
					auto* g = c ? c->AsGeometry() : nullptr;
					auto* ds = g ? g->AsDynamicTriShape() : nullptr;
					if (ds) {
						out.push_back(ds);
						if (s_logged.load() < 6) {
							if (Chatty()) logger::info("  node {} shape '{}' {} verts", n, ds->name.c_str(), ds->GetDynamicTrishapeRuntimeData().dataSize / 16);
						}
					}
				}
			}
			return out;
		}

		// Channels that are made of others: wide eyes are a blink the other way round, crossed eyes are both eyes looking at the nose
		struct Alias
		{
			const char* name;
			const char* target;
			float       factor;
		};
		const Alias kAlias[] = {
			{ "EyesWide", "BlinkLeft", -1.0f }, { "EyesWide", "BlinkRight", -1.0f },
			{ "EyeLeftWide", "BlinkLeft", -1.0f }, { "EyeRightWide", "BlinkRight", -1.0f },
			{ "EyesCross", "EyeLeftLookRight", 1.0f }, { "EyesCross", "EyeRightLookLeft", 1.0f },
			{ "EyesApart", "EyeLeftLookLeft", 1.0f }, { "EyesApart", "EyeRightLookRight", 1.0f },
		};

		// How open the mouth is as the engine draws it right now (OStim's open mouth, lip-sync mods: the MFG phonemes Aah, BigAah, Eh, Oh). Our own morphs are not in here.
		float NaturalMouth(RE::Actor* a)
		{
			auto* fg = a ? a->GetFaceGenAnimationData() : nullptr;
			if (!fg) {
				return 0.0f;
			}
			RE::BSSpinLockGuard lock(fg->lock);
			const auto&         k = fg->phoneme3;
			float               m = 0.0f;
			if (k.values && k.count >= 16) {
				for (const int i : { 0, 1, 6, 11 }) {
					m = std::max(m, k.values[i]);
				}
			}
			return m;
		}

// the 16 phonemes the engine draws right now, in MFG order (the same order as the names in kPhon)
		static const char* const kPhon[16] = { "Aah", "BigAah", "BMP", "ChJSh", "DST", "Eee", "Eh", "FV", "I", "K", "N", "Oh", "OohQ", "R", "Th", "W" };
		void ReadPhonemes(RE::Actor* a, float* out)
		{
			for (int i = 0; i < 16; ++i) {
				out[i] = 0.0f;
			}
			auto* fg = a ? a->GetFaceGenAnimationData() : nullptr;
			if (!fg) {
				return;
			}
			RE::BSSpinLockGuard lock(fg->lock);
			const auto&         k = fg->phoneme3;
			if (k.values && k.count >= 16) {
				for (int i = 0; i < 16; ++i) {
					out[i] = k.values[i];
				}
			}
		}

		struct OralCtx
		{
			bool  on{ false };
			float effNat{ 0.0f };     // OStim's mouth opening, or the bridged value while a rebuilt face has not got it back yet
			float yield{ 0.0f };      // 0..1: how far the mouth channels give way to it
			float addVec[16]{};       // per phoneme: what the bridge has to add on top of what the engine draws now (the whole pose, not one number)
			float bridgeAdd{ 0.0f };  // BigAah to add so the bridged opening is visible
		};

		bool IsMouthMorph(const std::string& n)
		{
			static const std::set<std::string> m{ "Aah", "BigAah", "BMP", "ChJSh", "DST", "Eee", "Eh", "FV", "I", "K", "N", "Oh", "OohQ", "R", "Th", "W" };
			return m.contains(n);
		}

		const std::vector<float>& MouthMask(const TriSet& t)
		{
			if (t.maskDone) {
				return t.mouthMask;
			}
			t.maskDone = true;
			t.mouthMask.assign(static_cast<std::size_t>(t.nv), 0.0f);
			for (const char* n : { "Aah", "BigAah", "BMP", "ChJSh", "DST", "Eee", "Eh", "FV", "I", "K", "N", "Oh", "OohQ", "R", "Th", "W" }) {
				const auto m = t.morphs.find(n);
				if (m == t.morphs.end()) {
					continue;
				}
				for (std::size_t v = 0; v < static_cast<std::size_t>(t.nv); ++v) {
					const float dx = m->second[v * 3], dy = m->second[v * 3 + 1], dz = m->second[v * 3 + 2];
					t.mouthMask[v] = std::max(t.mouthMask[v], std::sqrt(dx * dx + dy * dy + dz * dz));
				}
			}
			for (auto& x : t.mouthMask) {  // lips move a unit or more, the cheeks barely: a smooth step between 0.25 and 1.0 units
				const float k = std::clamp((x - 0.25f) / 0.75f, 0.0f, 1.0f);
				x = k * k * (3.0f - 2.0f * k);
			}
			return t.mouthMask;
		}

		std::vector<std::pair<std::string, float>> Expand(const std::map<std::string, float>& cur, RE::Actor* actor, const OralCtx& oc)
{
			std::vector<std::pair<std::string, float>> out;
			float                                      assist = 0.0f;
			for (const auto& [name, w] : cur) {
				if (std::fabs(w) < 0.0005f) {
					continue;
				}
				if (name == "MouthAssist") {  // BigAah while the natural mouth is shut, less and less as it opens (0.45 and more = none); a floor, not an addition
assist = w * std::clamp(1.0f - (oc.on ? oc.effNat : NaturalMouth(actor)) / 0.45f, 0.0f, 1.0f);
					continue;
				}
				bool alias = false;
				for (const auto& al : kAlias) {
					if (name == al.name) {
						out.push_back({ al.target, w * al.factor });
						alias = true;
					}
				}
				if (!alias) {
					out.push_back({ name, w });
				}
			}
if (oc.on && oc.yield > 0.0f) {  // oral scene: the mouth channels give way as OStim's own mouth opens (the two used to add up)
				for (auto& [n, w] : out) {
					if (IsMouthMorph(n)) {
						w *= 1.0f - oc.yield;
					}
				}
			}
			if (assist > 0.0005f) {
				float have = 0.0f;
				for (const auto& [n, w] : out) {
					if (n == "BigAah") {
						have += w;
					}
				}
				if (assist > have) {
out.push_back({ "BigAah", assist - have });
				}
			}
			if (oc.on && oc.bridgeAdd > 0.001f) {
				for (int i = 0; i < 16; ++i) {
						if (oc.addVec[i] > 0.0005f) {
							out.push_back({ kPhon[i], oc.addVec[i] });
						}
					}
			}
			return out;
		}

		std::string EyeWeightsText(const std::vector<std::pair<std::string, float>>& ex)
		{
			std::string t;
			for (const auto& [n, w] : ex) {
				if (n.find("Blink") != std::string::npos || n.find("Squint") != std::string::npos || n.find("Eye") != std::string::npos) {
					t += std::format("{}={:.2f} ", n, w);
				}
			}
			return t.empty() ? "(no eye channels)" : t;
		}

		// eye watch: does this actor hold unequal LEFT / RIGHT eye weights (blink, squint, per-eye look channels)?
		void WatchWeights(RE::Actor* a, ActorState& as, const std::vector<std::pair<std::string, float>>& ex)
		{
			std::map<std::string, std::pair<float, float>> pairs;  // channel without its side word -> (left, right)
			for (const auto& [name, w] : ex) {
				if (name.find("Blink") == std::string::npos && name.find("Squint") == std::string::npos && name.find("Eye") == std::string::npos) {
					continue;
				}
				std::string canon = name;
				int         side = -1;
				if (const auto p = canon.find("Left"); p != std::string::npos) {
					canon.erase(p, 4);
					side = 0;
				} else if (const auto q = canon.find("Right"); q != std::string::npos) {
					canon.erase(q, 5);
					side = 1;
				}
				if (side < 0) {
					continue;
				}
				(side == 0 ? pairs[canon].first : pairs[canon].second) += w;
			}
			std::string what;
			for (const auto& [c, lr] : pairs) {
				if (std::fabs(lr.first - lr.second) > 0.25f) {
					what += std::format("{}: left {:.2f} right {:.2f}; ", c, lr.first, lr.second);
				}
			}
			if (!what.empty()) {
				if (!as.wAsymOn) {
					as.wAsymOn = true;
					++g_weightAsym;
					if (g_watchLines.fetch_add(1) < 60) {
						logger::info("eye watch: UNEQUAL left/right eye weights on {} ({:08X}): {}", a->GetName(), a->GetFormID(), what);
					}
				}
			} else if (as.wAsymOn) {
				as.wAsymOn = false;
				if (g_watchLines.fetch_add(1) < 60) {
					logger::info("eye watch: left/right eye weights equal again on {} ({:08X})", a->GetName(), a->GetFormID());
				}
			}
		}

		// Adds the change in offsets (delta) to the live mesh AND to the engine's base vertices (FOD), so the engine's own rewrites and uploads carry them.
		void Apply(RE::Actor* a, ActorState& as, bool anyNonZero)
		{
OralCtx oc;
			if (const auto o = as.cur.find("OralMode"); o != as.cur.end() && o->second >= 0.5f) {
				oc.on = true;
				const auto  now = std::chrono::steady_clock::now();
				const float nat = NaturalMouth(a);
				if (nat >= as.hiNat || now - as.hiAt > std::chrono::milliseconds(600)) {
					as.hiNat = nat;
					ReadPhonemes(a, as.hiVec);
					as.hiAt = now;
				}
				if (as.rebuilt) {  // the engine rebuilt her face a moment ago: its mouth is shut until OStim's pose comes back
					as.rebuilt = false;
					if (now - as.hiAt < std::chrono::milliseconds(700) && as.hiNat > 0.1f) {
						as.bridgeVal = as.hiNat;
						std::copy(as.hiVec, as.hiVec + 16, as.bridgeVec);
						as.bridgeAt = now;
						if (Chatty()) logger::info("Morph: oral mode: bridging a mouth opening of {:.2f} over the rebuild", as.bridgeVal);
					}
				}
				oc.effNat = nat;
				if (as.bridgeVal > 0.0f) {
					const float el = std::chrono::duration<float>(now - as.bridgeAt).count();
					if (el < 4.2f) {  // hold the opening flat for 3.6 s (OStim's pose took ~3 s to come back in the log), then ease out over 0.6 s; max() hands over to the real pose the moment it returns
						oc.effNat = std::max(nat, as.bridgeVal * (el < 3.6f ? 1.0f : 1.0f - (el - 3.6f) / 0.6f));
					} else {
						as.bridgeVal = 0.0f;
					}
				}
				oc.yield = std::clamp(oc.effNat / 0.45f, 0.0f, 1.0f);
				oc.bridgeAdd = std::max(0.0f, oc.effNat - nat);
					if (as.bridgeVal > 0.0f) {  // the full pose: every phoneme OStim had drawn, minus what the engine draws now (so OStim's own pose takes over the moment it is back)
						const float el = std::chrono::duration<float>(now - as.bridgeAt).count();
						const float env = el < 3.6f ? 1.0f : std::max(0.0f, 1.0f - (el - 3.6f) / 0.6f);
						float       live[16];
						ReadPhonemes(a, live);
						for (int i = 0; i < 16; ++i) {
							oc.addVec[i] = std::max(0.0f, as.bridgeVec[i] * env - live[i]);
						}
					}
			}
			bool hadLooked = false;  // she already had measured shapes before this pass: a shape met for the first time now is a rebuilt one (a tongue equip makes the engine build NEW shape objects at NEW addresses)
			for (const auto& [sp, sr] : as.shapes) {
				if (sr.looked) {
					hadLooked = true;
					break;
				}
			}
			const auto expanded = Expand(Merged(as), a, oc);  // the same for every shape of this face: once per pass, not once per shape
			const float pz = g_poke.load();
			float sideMax[2]{ 0.0f, 0.0f };
			int   sideN[2]{ 0, 0 };
			bool  sideReset[2]{ false, false }, sideRewr[2]{ false, false };
			if (kEyeWatch) {
				WatchWeights(a, as, expanded);
			}
			const auto faceShapes = FaceShapes(a);
			int        passFam = -1;
			{
				std::vector<int> counts;
				for (auto* fs : faceShapes) {
					counts.push_back(static_cast<int>(fs->GetDynamicTrishapeRuntimeData().dataSize / 16));
				}
				passFam = FamilyOf(as.female, counts);
			}
			for (auto* ds : faceShapes) {
				auto&      rd = ds->GetDynamicTrishapeRuntimeData();
				const auto nv = static_cast<std::size_t>(rd.dataSize / 16);
				if (!rd.dynamicData) {
					continue;
				}
				const TriSet* set = SetFor(passFam, ds, static_cast<int>(nv), static_cast<const float*>(rd.dynamicData));
				if (!set) {
					continue;
				}
				auto& st = as.shapes[ds];
				const int side = kEyeWatch ? EyeSide(ds->name.c_str()) : -1;

					// the offset we want inside the shape now. Same inputs as the pass that built 'applied' = same offset: nothing to compute and nothing to write.
					bool reuse = st.haveKey && st.keySet == set && st.keyOral == oc.on && st.keyPoke == pz && st.applied.size() == nv * 3 && st.key == expanded;
					std::vector<float> want;
					const auto buildWant = [&]() {
						want.assign(nv * 3, 0.0f);
						for (const auto& [name, w] : expanded) {
							const Deltas* m = FindMorph(*set, name);
							if (!m) {
								continue;
							}
							// oral scene: everything that is not a mouth channel (the emotions) leaves the lips alone
							const float* mk = oc.on && !IsMouthMorph(name) ? MouthMask(*set).data() : nullptr;
							for (std::size_t i = 0; i < nv * 3; ++i) {
								want[i] += (*m)[i] * w * (mk ? 1.0f - mk[i / 3] : 1.0f);
							}
						}
						if (pz != 0.0f) {
							for (std::size_t v = 0; v < nv; ++v) {
								want[v * 3 + 2] += pz;
							}
						}
					};
					if (!reuse) {
						buildWant();
					}
					if (st.applied.size() != nv * 3) {
						st.applied.assign(nv * 3, 0.0f);
					}

				RE::BSSpinLockGuard lk(rd.lock);
				auto*               d = static_cast<float*>(rd.dynamicData);
				// A shape that was rebuilt (a scene ended and the engine redressed / refreshed the face) can land at the same address as the old one. Our record of what
				// offset is "inside" it would then be wrong, and fading the face out would subtract an offset that is not there. A new vertex buffer or a new base array = start over.
				if (st.looked && (st.dyn != rd.dynamicData || st.fod != FindFod(ds, nv))) {
					if (Chatty()) logger::info("Morph: '{}' was rebuilt by the engine (buffer {:p} -> {:p}): starting its record over", ds->name.c_str(), st.dyn, rd.dynamicData);
st = ShapeState{};
					if (side >= 0) {
						sideReset[side] = true;
						++g_side[side].resets;
					}
					st.applied.assign(nv * 3, 0.0f);
					as.rebuilt = true;
					if (reuse) {  // the record was just reset: 'applied' is zero again, so the offset has to be built after all
						buildWant();
						reuse = false;
					}
				}
				st.dyn = rd.dynamicData;
				if (!st.looked) {
					st.looked = true;
					if (hadLooked) {
						as.rebuilt = true;  // a new shape object for an actor we already knew: the face was rebuilt (same bridge as the same-address case below)
					}
					st.fod = FindFod(ds, nv);
					st.fodWorks = st.fod != nullptr;  // decided here and never changed: with a patched base array the engine's own rewrites already contain our offsets
					st.fmd = g_bump.load() ? FindFmd(ds, nv) : nullptr;  // g_bump is now the menu's "also patch the FMD copy" switch
					if (Chatty()) logger::info("Morph: base arrays of '{}' ({} verts): FOD {:p}, FMD {:p}", ds->name.c_str(), nv, static_cast<void*>(st.fod), static_cast<void*>(st.fmd));
				}

				// did the engine rewrite the mesh since our last write? then see whether it built it from the patched base
				if (st.lastOut.size() == nv * 3) {
					bool rewritten = false;
					for (std::size_t v = 0; v < nv && !rewritten; ++v) {
						for (std::size_t q = 0; q < 3; ++q) {
							if (std::fabs(d[v * 4 + q] - st.lastOut[v * 3 + q]) > 1e-4f) {
								rewritten = true;
								break;
							}
						}
					}
					if (rewritten && side >= 0) {
							sideRewr[side] = true;
							++g_side[side].rewritten;
						}
						if (rewritten) {
						float maxApplied = 0.0f, maxDev = 0.0f;
						for (std::size_t v = 0; v < nv; ++v) {
							const float ax = st.applied[v * 3], ay = st.applied[v * 3 + 1], az = st.applied[v * 3 + 2];
							const float am = std::sqrt(ax * ax + ay * ay + az * az);
							if (am > 0.3f) {
								maxApplied = std::max(maxApplied, am);
								const float dx = d[v * 4] - st.lastOut[v * 3], dy = d[v * 4 + 1] - st.lastOut[v * 3 + 1], dz = d[v * 4 + 2] - st.lastOut[v * 3 + 2];
								maxDev = std::max(maxDev, std::sqrt(dx * dx + dy * dy + dz * dz));
							}
						}
						if (maxApplied > 0.3f) {
							// Only for the log. A big difference from our last write is normally the engine ANIMATING the face (blink, lip sync, a gaze, a new expression), not our
						// offsets being lost: it used to flip fodWorks off here and add the offsets a second time on top of an output that already had them, which showed as a flash
						// of an exaggerated expression that then reverted (actors whose faces animate a lot, e.g. while speaking).
						const bool bigChange = maxDev >= 0.5f * maxApplied;
						if (st.verdicts < 8) {
							++st.verdicts;
							if (Chatty()) logger::info("Morph: the engine rewrote '{}': {} (largest offset {:.2f}, largest change from our last write {:.2f})", ds->name.c_str(),
								bigChange ? "a big change (animation) - offsets kept" : "offsets SURVIVED", maxApplied, maxDev);
						}
						}
						if (!st.fodWorks) {
							for (std::size_t v = 0; v < nv; ++v) {  // the engine's output has no offsets: put them back
								for (std::size_t q = 0; q < 3; ++q) {
									d[v * 4 + q] += st.applied[v * 3 + q];
								}
							}
						}
					}
				}

				++g_passAll;
				float maxd = 0.0f;
				if (reuse) {
					++g_passSkipped;
				}
				// apply the change (when the inputs are unchanged, 'want' would equal 'applied': every delta is zero, so the pass is skipped)
				if (!reuse) {
					for (std::size_t v = 0; v < nv; ++v) {
						for (std::size_t q = 0; q < 3; ++q) {
							const float delta = want[v * 3 + q] - st.applied[v * 3 + q];
							if (side >= 0) {
								maxd = std::max(maxd, std::fabs(delta));
							}
							if (std::fabs(delta) < 1e-6f) {
								continue;
							}
							d[v * 4 + q] += delta;
							if (st.fod) {
								st.fod[v * 3 + q] += delta;
							}
							if (st.fmd) {
								st.fmd[v * 3 + q] += delta;
							}
						}
					}
					st.applied = std::move(want);
					st.key = expanded;
					st.keySet = set;
					st.keyOral = oc.on;
					st.keyPoke = pz;
					st.haveKey = true;
				}
				if (side >= 0) {
					++sideN[side];
					++g_side[side].passes;
					sideMax[side] = std::max(sideMax[side], maxd);
					if (maxd > 0.02f) {
						++g_side[side].changed;
					}
				}
				st.lastOut.resize(nv * 3);
				for (std::size_t v = 0; v < nv; ++v) {
					for (std::size_t q = 0; q < 3; ++q) {
						st.lastOut[v * 3 + q] = d[v * 4 + q];
					}
				}
			}
			if (kEyeWatch && sideN[0] > 0 && sideN[1] > 0) {
				const bool oneSided = (sideMax[0] > 0.15f && sideMax[1] < 0.01f) || (sideMax[1] > 0.15f && sideMax[0] < 0.01f);
				if (oneSided) {
					++g_oneSided;
					const auto now = std::chrono::steady_clock::now();
					if (now - as.wLastLog > std::chrono::milliseconds(500) && g_watchLines.fetch_add(1) < 60) {
						as.wLastLog = now;
						logger::info("eye watch: ONE-SIDED change on {} ({:08X}): largest vertex change left eye shapes {:.2f}, right {:.2f}; engine rewrote left {} right {}; record reset left {} right {}; weights: {}",
							a->GetName(), a->GetFormID(), sideMax[0], sideMax[1], sideRewr[0], sideRewr[1], sideReset[0], sideReset[1], EyeWeightsText(expanded));
					}
				}
			}
			if (!anyNonZero && as.releasing && g_poke.load() == 0.0f) {
				as.active = false;
			}
		}

// ---- cost counters: logged every 30 s while any face is shown, so a frame-rate drop can be blamed or cleared ----
		struct PerfStat
		{
			std::atomic<std::uint64_t> sumUs{ 0 };
			std::atomic<std::uint64_t> maxUs{ 0 };
			std::atomic<std::uint64_t> n{ 0 };
		};
		PerfStat                                g_perfFrame, g_perfAfter;
		std::chrono::steady_clock::time_point   g_perfSince = std::chrono::steady_clock::now();

// The face nodes of every active actor, republished by Frame() each frame. The face-update hook reads this WITHOUT taking g_mx, so a face that is not ours
		// (almost all of them) never waits for Frame() to finish. A stale entry only makes the hook take the lock once for nothing.
		constexpr int                                   kHotMax = 64;
		std::atomic<RE::BSFaceGenNiNode*>               g_hotNodes[kHotMax];
		std::atomic<int>                                g_hotCount{ 0 };

		struct PerfScope
		{
			PerfStat&                             st;
			std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
			explicit PerfScope(PerfStat& a_st) : st(a_st) {}
			~PerfScope()
			{
				const auto us = static_cast<std::uint64_t>(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count());
				st.sumUs += us;
				++st.n;
				auto cur = st.maxUs.load();
				while (us > cur && !st.maxUs.compare_exchange_weak(cur, us)) {
				}
			}
		};

		void PerfReport()
		{
			const auto now = std::chrono::steady_clock::now();
			if (std::chrono::duration<float>(now - g_perfSince).count() < 30.0f) {
				return;
			}
			g_perfSince = now;
			std::size_t active = 0;
			{
				std::lock_guard lk(g_mx);
				for (const auto& [id, as] : g_act) {
					if (as.active) {
						++active;
					}
				}
			}
			const auto fn = g_perfFrame.n.exchange(0), an = g_perfAfter.n.exchange(0);
			const auto fs = g_perfFrame.sumUs.exchange(0), as_ = g_perfAfter.sumUs.exchange(0);
			const auto fm = g_perfFrame.maxUs.exchange(0), am = g_perfAfter.maxUs.exchange(0);
			const auto pa = g_passAll.exchange(0), pskip = g_passSkipped.exchange(0);
			if (Chatty() && active > 0 && fn > 0) {
				logger::info("perf: FaceLab frame avg {:.0f} us (max {} us) over {} frames; face-update hook avg {:.0f} us (max {} us), {} calls ({:.0f}/s); {} characters shown; offset rebuild skipped on {} of {} shape passes",
					static_cast<double>(fs) / static_cast<double>(fn), fm, fn, an ? static_cast<double>(as_) / static_cast<double>(an) : 0.0, am, an, static_cast<double>(an) / 30.0, active, pskip, pa);
				if (kEyeWatch) {
					const auto lp = g_side[0].passes.exchange(0), lr = g_side[0].rewritten.exchange(0), lx = g_side[0].resets.exchange(0), lc = g_side[0].changed.exchange(0);
					const auto rp = g_side[1].passes.exchange(0), rr = g_side[1].rewritten.exchange(0), rx = g_side[1].resets.exchange(0), rc = g_side[1].changed.exchange(0);
					logger::info("eye watch (30 s): left eye shapes {} passes / {} engine rewrites / {} resets / {} changed; right eye shapes {} / {} / {} / {}; one-sided changes {}; episodes of unequal left/right weights {}",
						lp, lr, lx, lc, rp, rr, rx, rc, g_oneSided.exchange(0), g_weightAsym.exchange(0));
				}
			}
		}

		void Frame()
		{
PerfScope ps_{ g_perfFrame };
			const auto now = std::chrono::steady_clock::now();
			const float dt = std::chrono::duration<float>(now - g_last).count();
			g_last = now;
			{
				std::lock_guard lk(g_mx);
				for (auto it = g_act.begin(); it != g_act.end();) {
					auto* a = RE::TESForm::LookupByID<RE::Actor>(it->first);
					if (!a) {
						it = g_act.erase(it);
						continue;
					}
					if (!a->Is3DLoaded()) {  // out of sight: keep the record, so a face that comes back is still taken off properly
						++it;
						continue;
					}
					if (it->second.active) {
it->second.female = a->GetActorBase() && a->GetActorBase()->GetSex() == RE::SEX::kFemale;
						it->second.nodeA = a->GetFaceNode();
						it->second.nodeB = a->GetFaceNodeSkinned();
						const bool nz = Ease(it->second, dt);
						Apply(a, it->second, nz);
					}
					if (!it->second.active && it->second.shapes.empty()) {
						it = g_act.erase(it);
						continue;
					}
if (!it->second.active) {
						it->second.shapes.clear();
					}
					++it;
				}
			}
{
				std::lock_guard lk(g_mx);
				int n = 0;
				for (const auto& [id, as] : g_act) {
					if (!as.active) {
						continue;
					}
					if (as.nodeA && n < kHotMax) {
						g_hotNodes[n++].store(as.nodeA, std::memory_order_relaxed);
					}
					if (as.nodeB && as.nodeB != as.nodeA && n < kHotMax) {
						g_hotNodes[n++].store(as.nodeB, std::memory_order_relaxed);
					}
				}
				g_hotCount.store(n, std::memory_order_release);
			}
			PerfReport();
		}


		// The engine rewrites the face mesh inside BSFaceGenNiNode::UpdateDownwardPass every time the face changes. Adding the offsets right after it, in the same pass,
		// means the mesh that is drawn always has them (adding them a frame later flickered back to the plain face while the game ran).
void AfterFaceUpdate(RE::BSFaceGenNiNode* self)
		{
			{
				const int n = g_hotCount.load(std::memory_order_acquire);
				bool      mine = false;
				for (int i = 0; i < n; ++i) {
					if (g_hotNodes[i].load(std::memory_order_relaxed) == self) {
						mine = true;
						break;
					}
				}
				if (!mine) {
					return;  // not one of ours: no lock, no work
				}
			}
			std::lock_guard lk(g_mx);
			PerfScope ps_{ g_perfAfter };
			if (g_act.empty()) {
				return;
			}
// This hook runs for EVERY face node the engine updates (about a thousand times a second in a busy place), and almost none of them are ours.
				// Frame() remembers the two face nodes of each active actor, so a node that is not ours costs a pointer compare, not an actor lookup.
				for (auto& [id, as] : g_act) {
					if (!as.active || (as.nodeA != self && as.nodeB != self)) {
						continue;
					}
					auto* a = RE::TESForm::LookupByID<RE::Actor>(id);
					if (a) {
						Apply(a, as, AnyNonZero(as));
					}
				}
		}

		struct FaceUpdateHook
		{
			static void thunk(RE::BSFaceGenNiNode* a_this, RE::NiUpdateData& a_data, std::uint32_t a_arg2)
			{
				func(a_this, a_data, a_arg2);
				AfterFaceUpdate(a_this);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		std::atomic<bool> g_pending{ false };
	}

	void Set(RE::Actor* actor, const std::vector<std::pair<std::string, float>>& weights, float fadeSeconds)
	{
		if (!actor) {
			return;
		}
		std::lock_guard lk(g_mx);
		auto&           as = g_act[actor->GetFormID()];
		as.female = actor->GetActorBase() && actor->GetActorBase()->GetSex() == RE::SEX::kFemale;
		as.fade = fadeSeconds;
		as.active = true;
		as.releasing = false;
		g_dbg = 80;
		for (auto& [name, w] : as.target) {
			w = 0.0f;  // channels not listed go back to zero
		}
		for (const auto& [name, w] : weights) {
			as.target[name] = w;
		}
	}

	void Clear(RE::Actor* actor, float fadeSeconds)
	{
		if (!actor) {
			return;
		}
		std::lock_guard lk(g_mx);
		const auto      it = g_act.find(actor->GetFormID());
		if (it == g_act.end()) {
			return;
		}
		if (fadeSeconds >= 0.0f) {
			it->second.fade = fadeSeconds;
		}
		for (auto& [name, w] : it->second.target) {
			w = 0.0f;
		}
		it->second.releasing = true;
	}

	void SetGaze(RE::FormID id, const std::vector<std::pair<std::string, float>>& weights, float fadeSeconds)
	{
		std::lock_guard lk(g_mx);
		auto&           as = g_act[id];  // the sex is filled in by Frame (it has the actor)
		as.gazeFade = fadeSeconds;
		as.active = true;
		as.releasing = false;
		for (auto& [name, w] : as.gazeTarget) {
			w = 0.0f;  // channels not listed go back to zero
		}
		for (const auto& [name, w] : weights) {
			as.gazeTarget[name] = w;
		}
	}

	void ClearGaze(RE::FormID id, float fadeSeconds)
	{
		std::lock_guard lk(g_mx);
		const auto      it = g_act.find(id);
		if (it == g_act.end()) {
			return;
		}
		if (fadeSeconds >= 0.0f) {
			it->second.gazeFade = fadeSeconds;
		}
		for (auto& [name, w] : it->second.gazeTarget) {
			w = 0.0f;
		}
		it->second.releasing = true;
	}

	void ClearAll()
	{
		std::lock_guard lk(g_mx);
		for (auto& [id, as] : g_act) {
			as.fade = 0.0f;
			as.gazeFade = 0.0f;
			for (auto& [name, w] : as.target) {
				w = 0.0f;
			}
			for (auto& [name, w] : as.gazeTarget) {
				w = 0.0f;
			}
			as.releasing = true;
		}
	}

	float MouthOpen(RE::FormID id)
	{
		std::lock_guard lk(g_mx);
		const auto      it = g_act.find(id);
		if (it == g_act.end()) {
			return -1.0f;
		}
		float m = 0.0f;
		for (const char* n : { "Aah", "BigAah", "Eh", "Oh" }) {
			if (const auto c = it->second.cur.find(n); c != it->second.cur.end()) {
				m = std::max(m, c->second);
			}
		}
		return m;
	}

	float Tongue(RE::FormID id)
	{
		std::lock_guard lk(g_mx);
		const auto      it = g_act.find(id);
		if (it == g_act.end()) {
			return -1.0f;
		}
		const auto c = it->second.cur.find("TongueOut");
		return c == it->second.cur.end() ? 0.0f : std::max(0.0f, c->second);
	}

	float EyeControl(RE::FormID id)
	{
		std::lock_guard lk(g_mx);
		const auto      it = g_act.find(id);
		if (it == g_act.end()) {
			return -1.0f;
		}
		// only the cross-eyed slider takes the eyes: any value above 0 and The Fourth Wall lets go (a little is already a different gaze)
		const auto c = it->second.cur.find("EyesCross");
		return c == it->second.cur.end() ? 0.0f : std::clamp(std::fabs(c->second) / 0.1f, 0.0f, 1.0f);
	}

	bool Supported(RE::Actor* actor)
	{
		if (!actor || !actor->Get3D()) {
			return false;
		}
		const bool       female = actor->GetActorBase() && actor->GetActorBase()->GetSex() == RE::SEX::kFemale;
		std::vector<int> counts;
		std::string      shapes;
		for (auto* ds : FaceShapes(actor)) {
			const int n = static_cast<int>(ds->GetDynamicTrishapeRuntimeData().dataSize / 16);
			counts.push_back(n);
			shapes += std::format(" {}={}", ds->name.c_str(), n);
		}
		const int fam = FamilyOf(female, counts);
		// supported = a head family fits: the head itself is one we have morphs for, or at least two different face parts match
		static std::map<RE::FormID, int> told;
		const auto                       it = told.find(actor->GetFormID());
		if (it == told.end() || it->second != fam) {
			told[actor->GetFormID()] = fam;
			if (fam >= 0) {
				logger::info("Morph: {} uses head family '{}' (face shapes:{})", actor->GetDisplayFullName(), Fam::All()[static_cast<std::size_t>(fam)].name, shapes);
			} else {
				logger::info("Morph: {} is not supported: none of the head families fits (face shapes:{})", actor->GetDisplayFullName(), shapes);
			}
		}
		if (fam >= 0) {
			StartLoad(fam);  // read the family's morph files in the background now, so the face is ready when it is first asked for
		}
		return fam >= 0;
	}

	bool HeadNames(RE::Actor* actor, std::string& family, std::vector<NameInfo>& out)
	{
		out.clear();
		if (!actor || !actor->Get3D()) {
			return false;
		}
		const bool       female = actor->GetActorBase() && actor->GetActorBase()->GetSex() == RE::SEX::kFemale;
		std::vector<int> counts;
		for (auto* ds : FaceShapes(actor)) {
			counts.push_back(static_cast<int>(ds->GetDynamicTrishapeRuntimeData().dataSize / 16));
		}
		const int fam = FamilyOf(female, counts);
		if (fam < 0) {
			return false;
		}
		family = Fam::All()[static_cast<std::size_t>(fam)].name;
		auto& fr = *g_fam[static_cast<std::size_t>(fam)];
		if (fr.state.load() != 2) {
			StartLoad(fam);
			return false;  // still reading: ask again
		}
		const TriSet* head = nullptr;
		for (const auto& p : fr.parts) {
			if (p->nv == fr.headNv) {
				head = p.get();
				break;
			}
		}
		if (!head) {
			return true;
		}
		std::set<std::string> standard;
		for (const auto& ch : Channels()) {
			standard.insert(ch.name);
		}
		int others = 0;
		for (const auto& p : fr.parts) {
			if (p.get() != head) {
				++others;
			}
		}
		for (const auto& [name, dl] : head->morphs) {
			if (standard.contains(name)) {
				continue;
			}
			NameInfo ni;
			ni.name = name;
			ni.partsTotal = others;
			for (const auto& p : fr.parts) {
				if (p.get() != head && p->morphs.contains(name)) {
					++ni.parts;
				}
			}
			out.push_back(std::move(ni));
		}
		return true;
	}

	int SupportState(RE::Actor* actor)
	{
		if (!actor || !actor->Get3D() || FaceShapes(actor).empty()) {
			return -1;  // no face built yet: unknown
		}
		return Supported(actor) ? 1 : 0;
	}

	void Poke(RE::Actor* actor, float zUnits, float seconds)
	{
		if (!actor) {
			return;
		}
		Set(actor, {}, 0.0f);
		g_poke = zUnits;
		logger::info("Poke: the face mesh of {} is moved {} units up for {} s", actor->GetDisplayFullName(), zUnits, seconds);
		const auto h = actor->GetHandle();
		std::thread([h, seconds]() {
			std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(seconds * 1000.0f)));
			g_poke = 0.0f;
			SKSE::GetTaskInterface()->AddTask([h]() {
				if (auto a = h.get()) {
					Clear(a.get());
				}
			});
		}).detach();
	}

	void SetBump(bool on)
	{
		g_bump = on;
	}

	bool Active(RE::Actor* actor)
	{
		if (!actor) {
			return false;
		}
		std::lock_guard lk(g_mx);
		const auto      it = g_act.find(actor->GetFormID());
		return it != g_act.end() && it->second.active && !it->second.releasing;
	}

	// A task must never queue itself: SKSE runs its queue until it is empty, so that never returns. A timer thread queues one update at a time instead.
	void StartFrameLoop()
	{
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_BSFaceGenNiNode[0] };
		FaceUpdateHook::func = vtbl.write_vfunc(0x2C, FaceUpdateHook::thunk);
		logger::info("Morph: hooked BSFaceGenNiNode::UpdateDownwardPass");
		std::thread([]() {
			for (;;) {
				std::this_thread::sleep_for(std::chrono::milliseconds(6));
				bool expected = false;
				if (g_pending.compare_exchange_strong(expected, true)) {
					SKSE::GetTaskInterface()->AddTask([]() {
						Frame();
						g_pending = false;
					});
				}
			}
		}).detach();
	}
}
