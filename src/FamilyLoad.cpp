// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - FaceLab. See LICENSE.

#include "FamilyLoad.h"

#include "TriFile.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string_view>
#include <fstream>

namespace FaceLab::Fam
{
	namespace
	{
		std::string Lower(std::string s)
		{
			for (auto& c : s) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return s;
		}

		// mean distance between two base-vertex lists of the same length (the same mesh gives ~0; another mesh with the same vertex count gives a lot)
		double MeanDistance(const std::vector<float>& a, const std::vector<float>& b)
		{
			if (a.size() != b.size() || a.empty()) {
				return 0.0;
			}
			double sum = 0.0;
			const std::size_t n = a.size() / 3;
			for (std::size_t i = 0; i < n; ++i) {
				const double dx = a[i * 3] - b[i * 3], dy = a[i * 3 + 1] - b[i * 3 + 1], dz = a[i * 3 + 2] - b[i * 3 + 2];
				sum += std::sqrt(dx * dx + dy * dy + dz * dz);
			}
			return sum / static_cast<double>(n);
		}

		// One eye at a time: every Look morph again with only the vertices of her left (x < 0) or right (x > 0) side. The eyes, the lids in the head and the brows all split the same way.
		// Pure-rotation eye turns (EyeLeftTurnUp ... EyeRightTurnRight): the shipped Look morphs mostly SLIDE the eyeball up or down, which exposes the dark baked rim of the eye
		// texture under the lid when looking up. These turn the eyeball about its own centre instead: 10 degrees per 1.0 of weight. Only for the eye files.
		// Centre of the sphere an eyeball is part of (algebraic least-squares fit of |p|^2 = 2 c.p + k over the given vertices). An add-on eye pack draws only a front CAP of the
		// eyeball (Advanced Eyes Addon: 0.84 deep on a sphere of radius 1.23), whose centroid sits 1.1 units in front of the true centre; turning it about the centroid tilts the
		// cap in place instead of turning the eye. false = could not fit (too few or flat vertices): the caller keeps the centroid.
		bool FitSphereCentre(const Part& p, const std::vector<int>& side, int s, double& cx, double& cy, double& cz)
		{
			double N[4][5] = {};
			int    n = 0;
			for (std::size_t i = 0; i < static_cast<std::size_t>(p.nv); ++i) {
				if (side[i] != s) {
					continue;
				}
				const double x = p.base[i * 3], y = p.base[i * 3 + 1], z = p.base[i * 3 + 2];
				const double row[4] = { 2 * x, 2 * y, 2 * z, 1.0 };
				const double rhs = x * x + y * y + z * z;
				for (int a = 0; a < 4; ++a) {
					for (int b = 0; b < 4; ++b) {
						N[a][b] += row[a] * row[b];
					}
					N[a][4] += row[a] * rhs;
				}
				++n;
			}
			if (n < 16) {
				return false;
			}
			for (int c = 0; c < 4; ++c) {
				int piv = c;
				for (int r = c + 1; r < 4; ++r) {
					if (std::fabs(N[r][c]) > std::fabs(N[piv][c])) {
						piv = r;
					}
				}
				if (std::fabs(N[piv][c]) < 1e-9) {
					return false;
				}
				for (int k = 0; k < 5; ++k) {
					std::swap(N[c][k], N[piv][k]);
				}
				for (int r = 0; r < 4; ++r) {
					if (r != c) {
						const double f = N[r][c] / N[c][c];
						for (int k = 0; k < 5; ++k) {
							N[r][k] -= f * N[c][k];
						}
					}
				}
			}
			cx = N[0][4] / N[0][0];
			cy = N[1][4] / N[1][1];
			cz = N[2][4] / N[2][2];
			const double r2 = N[3][4] / N[3][3] + cx * cx + cy * cy + cz * cz;
			return std::isfinite(cx) && std::isfinite(cy) && std::isfinite(cz) && r2 > 0.04 && r2 < 100.0;
		}

		// Where to pivot instead of the fit of this file: the iris lens is a far shallower cap than the eyeball, so its own fit is loose; it has to turn about the eyeball's centre.
		struct PivotHint
		{
			bool   has{ false };
			double c[2][3]{};  // [0] left (x < 0), [1] right (x > 0)
		};

		// pack = an add-on eye pack (see LoadFile): its eyeball is a cap, so the turn pivots about the fitted sphere centre, every vertex turns the same, and the per-eye look UP / DOWN
		// become true turns (the shipped ones slide the whole cap, which pulls its dark rim into view above the iris). gain / gainDown say how strong a unit of look should be.
		// UBE's own eye files (Data/Meshes/!UBE/Eyes/*.tri): measured side by side in game, a UBE head turns far less than a vanilla-style head for the same weight (the shipped
		// look morphs move the eye about 0.1-0.2 units per 1.0 and only slide it). The per-eye look channels (not the combined Look morphs other mods use) are built differently:
		// sideways and down become true turns (UbeTurn), up keeps the shipped morph with a gain (UbeGain). Both can be overridden with one text file each in Data/SKSE/Plugins/FaceLab/.
		bool IsUbeEyePath(std::string path)
		{
			std::replace(path.begin(), path.end(), '\\', '/');
			std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return ("/" + path).find("/!ube/eyes/") != std::string::npos;
		}

		const std::pair<float, float> kUbeUnit{ 1.0f, 1.0f };  // UBE eyes are built at unit strength; Morph multiplies the per-eye look channels live (Lab: ubeSide / ubeDown / ubeUp)

		void AddEyeMorphs(Part& out, bool eyeFile, float gain = 1.0f, float gainDown = 1.0f, bool pack = false, const PivotHint* hint = nullptr, const std::pair<float, float>* ubeTurn = nullptr)
		{
			const int nv = out.nv;
			if (out.base.size() != static_cast<std::size_t>(nv) * 3) {
				return;
			}
			std::vector<int> side(static_cast<std::size_t>(nv), 0);
			for (int i = 0; i < nv; ++i) {
				const float x = out.base[static_cast<std::size_t>(i) * 3];
				side[static_cast<std::size_t>(i)] = x < -0.01f ? -1 : (x > 0.01f ? 1 : 0);
			}
			for (const char* dir : { "Left", "Right", "Up", "Down" }) {
				const auto src = out.morphs.find(std::string("Look") + dir);
				if (src == out.morphs.end()) {
					continue;
				}
				for (const int s : { -1, 1 }) {
					std::vector<float> dl = src->second;
					for (std::size_t i = 0; i < static_cast<std::size_t>(nv); ++i) {
						if (side[i] != s) {
							dl[i * 3] = dl[i * 3 + 1] = dl[i * 3 + 2] = 0.0f;
						}
					}
					const float gn = std::string_view(dir) == "Down" ? gainDown : gain;  // AEA's LookDown is the weakest (~55% of vanilla)
					if (gn != 1.0f) {
						for (auto& x : dl) {
							x *= gn;
						}
					}
					out.morphs[std::string(s < 0 ? "EyeLeftLook" : "EyeRightLook") + dir] = std::move(dl);
				}
			}
			if (!eyeFile || !out.morphs.contains("LookLeft")) {
				return;
			}
			const float a = 10.0f * 3.14159265f / 180.0f;
			const float sn = std::sin(a), cs = std::cos(a);
			// How much each vertex may turn: the shipped Look morphs move the front of the eye a lot and the rim next to the lids and lashes hardly at all
			// (a rigid turn pulled the rim away from the lids). So each vertex turns by min(1, its largest shipped move / 0.15).
			std::vector<float> mask(static_cast<std::size_t>(nv), 1.0f);
			for (int i = 0; i < nv; ++i) {
				float m = 0.0f;
				for (const char* nm : { "LookLeft", "LookRight", "LookUp", "LookDown" }) {
					if (const auto it = out.morphs.find(nm); it != out.morphs.end()) {
						const auto& dd = it->second;
						const auto  k = static_cast<std::size_t>(i) * 3;
						m = std::max(m, std::sqrt(dd[k] * dd[k] + dd[k + 1] * dd[k + 1] + dd[k + 2] * dd[k + 2]));
					}
				}
				mask[static_cast<std::size_t>(i)] = pack ? 1.0f : std::min(1.0f, m / 0.15f);  // a cap must turn as one piece
			}
			for (const int s : { -1, 1 }) {
				double cx = 0, cy = 0, cz = 0;
				int    n = 0;
				for (int i = 0; i < nv; ++i) {
					if (side[static_cast<std::size_t>(i)] != s) {
						continue;
					}
					cx += out.base[static_cast<std::size_t>(i) * 3];
					cy += out.base[static_cast<std::size_t>(i) * 3 + 1];
					cz += out.base[static_cast<std::size_t>(i) * 3 + 2];
					++n;
				}
				if (n < 8) {
					continue;
				}
				cx /= n;
				cy /= n;
				cz /= n;
				if (pack) {
					double fx = 0, fy = 0, fz = 0;
					if (hint && hint->has) {
						fx = hint->c[s < 0 ? 0 : 1][0];
						fy = hint->c[s < 0 ? 0 : 1][1];
						fz = hint->c[s < 0 ? 0 : 1][2];
						cx = fx;
						cy = fy;
						cz = fz;
					} else if (FitSphereCentre(out, side, s, fx, fy, fz)) {
						cx = fx;
						cy = fy;
						cz = fz;
					}
				}
				const char* names[4] = { "Left", "Right", "Up", "Down" };
				for (int dirIdx = 0; dirIdx < 4; ++dirIdx) {
					std::vector<float> dl(static_cast<std::size_t>(nv) * 3, 0.0f);
					for (int i = 0; i < nv; ++i) {
						if (side[static_cast<std::size_t>(i)] != s) {
							continue;
						}
						const float px = out.base[static_cast<std::size_t>(i) * 3] - static_cast<float>(cx), py = out.base[static_cast<std::size_t>(i) * 3 + 1] - static_cast<float>(cy),
						            pz = out.base[static_cast<std::size_t>(i) * 3 + 2] - static_cast<float>(cz);
						float nx = px, ny = py, nz = pz;
						switch (dirIdx) {
						case 0:  // look to her left: the front of the eye moves to -x (rotate about up)
							nx = px * cs - py * sn;
							ny = px * sn + py * cs;
							break;
						case 1:  // look to her right
							nx = px * cs + py * sn;
							ny = -px * sn + py * cs;
							break;
						case 2:  // look up: the front moves to +z (rotate about her right axis)
							ny = py * cs - pz * sn;
							nz = py * sn + pz * cs;
							break;
						default:  // look down
							ny = py * cs + pz * sn;
							nz = -py * sn + pz * cs;
							break;
						}
						const float mk = mask[static_cast<std::size_t>(i)];
						dl[static_cast<std::size_t>(i) * 3] = (nx - px) * mk;
						dl[static_cast<std::size_t>(i) * 3 + 1] = (ny - py) * mk;
						dl[static_cast<std::size_t>(i) * 3 + 2] = (nz - pz) * mk;
					}
					out.morphs[std::string(s < 0 ? "EyeLeftTurn" : "EyeRightTurn") + names[dirIdx]] = std::move(dl);
				}
				if (pack) {
					// per-eye look up / down as true turns: a unit of the shipped look turned the eye about 4.8 degrees (and slid it); keep that strength (x gain) but as a turn.
					// Up is a small share on purpose: Fourth Wall sends TurnUp as well and the two add. Down has no turn channel of its own in Fourth Wall, so it carries all of it.
					const std::string pre = s < 0 ? "EyeLeft" : "EyeRight";
					const std::pair<const char*, float> conv[2] = { { "Up", gain * 0.25f }, { "Down", gainDown * 0.5f } };
					for (const auto& [dn, k] : conv) {
						if (const auto it = out.morphs.find(pre + "Turn" + dn); it != out.morphs.end()) {
							std::vector<float> t = it->second;
							for (auto& v : t) {
								v *= k;
							}
							out.morphs[pre + "Look" + dn] = std::move(t);
						}
					}
				}
				if (ubeTurn) {
					// UBE eyes: the shipped Look morphs barely move the eye (down is the weakest, about 0.1 units per 1.0) and only slide it. The per-eye look channels Fourth Wall
					// sends become true turns instead, with the strength found by comparing a UBE character with a vanilla-style one in game (a turn of 3.0 matched a look-down
					// of 1.0, and a turn of about 2.1 per 1.0 matched sideways). Up stays the shipped morph (with its gain): Fourth Wall adds its own turn up.
					const std::string pre = s < 0 ? "EyeLeft" : "EyeRight";
					// All four are true turns at 10 degrees per 1.0 now (Up too: the shipped UBE LookUp is tiny, so it did nothing); Morph scales them live by the menu sliders.
					const std::pair<const char*, float> conv[4] = { { "Left", ubeTurn->first }, { "Right", ubeTurn->first }, { "Down", ubeTurn->second }, { "Up", ubeTurn->first } };
					for (const auto& [dn, k] : conv) {
						if (const auto it = out.morphs.find(pre + "Turn" + dn); it != out.morphs.end()) {
							std::vector<float> t = it->second;
							for (auto& v : t) {
								v *= k;
							}
							out.morphs[pre + "Look" + dn] = std::move(t);
						}
					}
				}
			}
		}

		bool Has(const std::string& hay, const char* needle)
		{
			return Lower(hay).find(needle) != std::string::npos;
		}

		// the parts that sit on or inside the head and have to move with it: the mouth (teeth, tongue), brows, lashes, eye shadows
		bool IsFollower(const Part& p)
		{
			for (const auto& r : p.rels) {
				if (Has(r, "mouth") || Has(r, "brows") || Has(r, "lashes") || Has(r, "shadow")) {
					return true;
				}
			}
			return false;
		}

		// Every morph of the head, carried over to a part that follows it: each vertex of the part takes the weighted mean of the offsets of its nearest head vertices
		// (inverse square distance), so the teeth and tongue follow the lips and jaw, the lashes the lids, the brows the brow ridge, whatever the expression is called.
		void FollowHead(const Part& head, Part& target, std::vector<std::string>* log)
		{
			constexpr int K = 6;
			const int     hn = head.nv, tn = target.nv;
			if (head.base.size() != static_cast<std::size_t>(hn) * 3 || target.base.size() != static_cast<std::size_t>(tn) * 3) {
				return;
			}
			std::vector<int>   idx(static_cast<std::size_t>(tn) * K, 0);
			std::vector<float> wt(static_cast<std::size_t>(tn) * K, 0.0f);
			double             meanNearest = 0.0;
			for (int v = 0; v < tn; ++v) {
				const float px = target.base[static_cast<std::size_t>(v) * 3], py = target.base[static_cast<std::size_t>(v) * 3 + 1], pz = target.base[static_cast<std::size_t>(v) * 3 + 2];
				float       bd[K];
				int         bi[K];
				for (int k = 0; k < K; ++k) {
					bd[k] = 1e30f;
					bi[k] = 0;
				}
				for (int u = 0; u < hn; ++u) {
					const float dx = head.base[static_cast<std::size_t>(u) * 3] - px, dy = head.base[static_cast<std::size_t>(u) * 3 + 1] - py, dz = head.base[static_cast<std::size_t>(u) * 3 + 2] - pz;
					const float d2 = dx * dx + dy * dy + dz * dz;
					if (d2 >= bd[K - 1]) {
						continue;
					}
					int k = K - 1;
					while (k > 0 && bd[k - 1] > d2) {
						bd[k] = bd[k - 1];
						bi[k] = bi[k - 1];
						--k;
					}
					bd[k] = d2;
					bi[k] = u;
				}
				float sum = 0.0f;
				for (int k = 0; k < K; ++k) {
					const float w = 1.0f / (bd[k] + 0.0025f);  // inverse square distance; the 0.05 unit floor keeps a vertex that sits on a head vertex from taking only that one
					wt[static_cast<std::size_t>(v) * K + static_cast<std::size_t>(k)] = w;
					idx[static_cast<std::size_t>(v) * K + static_cast<std::size_t>(k)] = bi[k];
					sum += w;
				}
				for (int k = 0; k < K; ++k) {
					wt[static_cast<std::size_t>(v) * K + static_cast<std::size_t>(k)] /= sum;
				}
				meanNearest += std::sqrt(bd[0]);
			}
			int carried = 0;
			for (const auto& [name, hd] : head.morphs) {
				std::vector<float> out(static_cast<std::size_t>(tn) * 3, 0.0f);
				bool               any = false;
				for (int v = 0; v < tn; ++v) {
					float ox = 0.0f, oy = 0.0f, oz = 0.0f;
					for (int k = 0; k < K; ++k) {
						const float w = wt[static_cast<std::size_t>(v) * K + static_cast<std::size_t>(k)];
						const auto  u = static_cast<std::size_t>(idx[static_cast<std::size_t>(v) * K + static_cast<std::size_t>(k)]) * 3;
						ox += w * hd[u];
						oy += w * hd[u + 1];
						oz += w * hd[u + 2];
					}
					out[static_cast<std::size_t>(v) * 3] = ox;
					out[static_cast<std::size_t>(v) * 3 + 1] = oy;
					out[static_cast<std::size_t>(v) * 3 + 2] = oz;
					if (ox != 0.0f || oy != 0.0f || oz != 0.0f) {
						any = true;
					}
				}
				if (any) {
					target.morphs[name] = std::move(out);
					++carried;
				}
			}
			if (log) {
				log->push_back(std::format("{} vertices follow the head ({} morphs carried over, mean distance to the nearest head vertex {:.2f}): {}", tn, carried, meanNearest / tn, target.rels.empty() ? std::string{} : target.rels.front()));
			}
		}

		// ---- the mouth (teeth, tongue, throat) is rigid: it must not stretch like skin ----
		struct Mat3
		{
			double m[3][3];
		};

		double Det(const Mat3& a)
		{
			return a.m[0][0] * (a.m[1][1] * a.m[2][2] - a.m[1][2] * a.m[2][1]) - a.m[0][1] * (a.m[1][0] * a.m[2][2] - a.m[1][2] * a.m[2][0]) + a.m[0][2] * (a.m[1][0] * a.m[2][1] - a.m[1][1] * a.m[2][0]);
		}

		bool InverseTranspose(const Mat3& a, Mat3& out)
		{
			const double d = Det(a);
			if (std::fabs(d) < 1e-18) {
				return false;
			}
			const auto& m = a.m;
			// inverse = adjugate / det; we want its transpose
			out.m[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) / d;
			out.m[0][1] = -(m[1][0] * m[2][2] - m[1][2] * m[2][0]) / d;
			out.m[0][2] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) / d;
			out.m[1][0] = -(m[0][1] * m[2][2] - m[0][2] * m[2][1]) / d;
			out.m[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / d;
			out.m[1][2] = -(m[0][0] * m[2][1] - m[0][1] * m[2][0]) / d;
			out.m[2][0] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / d;
			out.m[2][1] = -(m[0][0] * m[1][2] - m[0][2] * m[1][0]) / d;
			out.m[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / d;
			return true;
		}

		// the rotation closest to A (the orthogonal factor of its polar decomposition); identity when A is degenerate
		Mat3 PolarRotation(Mat3 a)
		{
			Mat3 id{ { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } };
			for (int it = 0; it < 40; ++it) {
				Mat3 it2;
				if (!InverseTranspose(a, it2)) {
					return id;
				}
				double diff = 0.0;
				for (int i = 0; i < 3; ++i) {
					for (int j = 0; j < 3; ++j) {
						const double n = 0.5 * (a.m[i][j] + it2.m[i][j]);
						diff += std::fabs(n - a.m[i][j]);
						a.m[i][j] = n;
					}
				}
				if (diff < 1e-10) {
					break;
				}
			}
			return Det(a) > 0.0 ? a : id;
		}

		// R limited to maxDeg degrees (a jaw does not turn further): rotate by the same axis, capped angle
		Mat3 CapRotation(const Mat3& r, double maxDeg)
		{
			const double tr = r.m[0][0] + r.m[1][1] + r.m[2][2];
			const double ang = std::acos(std::clamp((tr - 1.0) * 0.5, -1.0, 1.0));
			const double cap = maxDeg * 3.14159265358979 / 180.0;
			if (ang <= cap || ang < 1e-9) {
				return r;
			}
			double ax = r.m[2][1] - r.m[1][2], ay = r.m[0][2] - r.m[2][0], az = r.m[1][0] - r.m[0][1];
			const double n = std::sqrt(ax * ax + ay * ay + az * az);
			if (n < 1e-12) {
				return r;
			}
			ax /= n;
			ay /= n;
			az /= n;
			const double c = std::cos(cap), s = std::sin(cap), t = 1.0 - c;
			Mat3         o{ { { t * ax * ax + c, t * ax * ay - s * az, t * ax * az + s * ay },
				{ t * ax * ay + s * az, t * ay * ay + c, t * ay * az - s * ax },
				{ t * ax * az - s * ay, t * ay * az + s * ax, t * az * az + c } } };
			return o;
		}

		// The mouth part: every vertex belongs more or less to the lower jaw (its own jaw-open morph, BigAah, says how much: the lower teeth and the tongue move, the upper
		// teeth and the throat hardly). For each head morph, the head's lower jaw is fitted with ONE rigid motion (rotation + shift) from the head vertices that move with
		// its jaw, and the lower-jaw vertices of the mouth part get exactly that motion: teeth and tongue keep their shape instead of stretching with the lips.
		constexpr double kJawTurn = 0.5;  // 0 = the jaw only shifts, 1 = shifts and turns exactly like the head's jaw

		void FollowJaw(const Part& head, Part& target, std::vector<std::string>* log)
		{
			const int hn = head.nv, tn = target.nv;
			const auto ownIt = target.morphs.find("BigAah");
			const auto headIt = head.morphs.find("BigAah");
			if (ownIt == target.morphs.end() || headIt == head.morphs.end() || head.base.size() != static_cast<std::size_t>(hn) * 3 || target.base.size() != static_cast<std::size_t>(tn) * 3) {
				if (log) {
					log->push_back("the mouth part has no BigAah to tell its jaw by: it follows the head like skin instead: " + (target.rels.empty() ? std::string{} : target.rels.front()));
				}
				FollowHead(head, target, log);
				return;
			}
			const auto mag = [](const std::vector<float>& d, int v) {
				const auto k = static_cast<std::size_t>(v) * 3;
				return std::sqrt(d[k] * d[k] + d[k + 1] * d[k + 1] + d[k + 2] * d[k + 2]);
			};
			// how much of the lower jaw each mouth vertex is
			std::vector<float> wJaw(static_cast<std::size_t>(tn), 0.0f);
			float              maxOwn = 0.0f;
			for (int v = 0; v < tn; ++v) {
				maxOwn = std::max(maxOwn, mag(ownIt->second, v));
			}
			if (maxOwn < 1e-6f) {
				return;
			}
			for (int v = 0; v < tn; ++v) {
				const float r = mag(ownIt->second, v) / maxOwn;
				const float x = std::clamp((r - 0.10f) / 0.50f, 0.0f, 1.0f);
				wJaw[static_cast<std::size_t>(v)] = x * x * (3.0f - 2.0f * x);  // smoothstep
			}
			// the head vertices around the mouth, and how much each moves with the jaw (the head's own BigAah)
			float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
			for (int v = 0; v < tn; ++v) {
				for (int k = 0; k < 3; ++k) {
					lo[k] = std::min(lo[k], target.base[static_cast<std::size_t>(v) * 3 + static_cast<std::size_t>(k)]);
					hi[k] = std::max(hi[k], target.base[static_cast<std::size_t>(v) * 3 + static_cast<std::size_t>(k)]);
				}
			}
			float maxHead = 0.0f;
			std::vector<int> region;
			for (int u = 0; u < hn; ++u) {
				const float x = head.base[static_cast<std::size_t>(u) * 3], y = head.base[static_cast<std::size_t>(u) * 3 + 1], z = head.base[static_cast<std::size_t>(u) * 3 + 2];
				if (x < lo[0] - 4.0f || x > hi[0] + 4.0f || y < lo[1] - 4.0f || y > hi[1] + 4.0f || z < lo[2] - 8.0f || z > hi[2] + 2.0f) {
					continue;
				}
				region.push_back(u);
				maxHead = std::max(maxHead, mag(headIt->second, u));
			}
			std::vector<std::pair<int, float>> jaw;  // head vertex, weight
			for (const int u : region) {
				const float r = mag(headIt->second, u) / std::max(maxHead, 1e-6f);
				if (r > 0.25f) {
					jaw.push_back({ u, std::min(1.0f, (r - 0.25f) / 0.5f) });
				}
			}
			if (jaw.size() < 20) {
				if (log) {
					log->push_back("too few head vertices move with the jaw: the mouth part follows the head like skin instead");
				}
				FollowHead(head, target, log);
				return;
			}
			int carried = 0;
			for (const auto& [name, hd] : head.morphs) {
				// weighted centroids of the jaw before and after
				double wsum = 0.0, cp[3] = { 0, 0, 0 }, cq[3] = { 0, 0, 0 };
				for (const auto& [u, w] : jaw) {
					const auto k = static_cast<std::size_t>(u) * 3;
					for (int c = 0; c < 3; ++c) {
						cp[c] += w * head.base[k + static_cast<std::size_t>(c)];
						cq[c] += w * (head.base[k + static_cast<std::size_t>(c)] + hd[k + static_cast<std::size_t>(c)]);
					}
					wsum += w;
				}
				for (int c = 0; c < 3; ++c) {
					cp[c] /= wsum;
					cq[c] /= wsum;
				}
				Mat3 a{};
				for (const auto& [u, w] : jaw) {
					const auto k = static_cast<std::size_t>(u) * 3;
					double     p[3], q[3];
					for (int c = 0; c < 3; ++c) {
						p[c] = head.base[k + static_cast<std::size_t>(c)] - cp[c];
						q[c] = head.base[k + static_cast<std::size_t>(c)] + hd[k + static_cast<std::size_t>(c)] - cq[c];
					}
					for (int i = 0; i < 3; ++i) {
						for (int j = 0; j < 3; ++j) {
							a.m[i][j] += w * q[i] * p[j];
						}
					}
				}
				const Mat3 rot = CapRotation(PolarRotation(a), 40.0);
				std::vector<float> out(static_cast<std::size_t>(tn) * 3, 0.0f);
				bool               any = false;
				for (int v = 0; v < tn; ++v) {
					const float w = wJaw[static_cast<std::size_t>(v)];
					if (w <= 0.0f) {
						continue;
					}
					const auto   k = static_cast<std::size_t>(v) * 3;
					const double px = target.base[k] - cp[0], py = target.base[k + 1] - cp[1], pz = target.base[k + 2] - cp[2];
					// the jaw's shift, plus only a part (kJawTurn) of its turn: a morph is blended in a straight line, so a full rotation stretches the teeth as soon as the weight
					// goes above 1 (FaceLab goes to 3). Mostly shift keeps teeth and tongue the shape they have at any weight.
					const double nx = (cq[0] - cp[0]) + kJawTurn * (rot.m[0][0] * px + rot.m[0][1] * py + rot.m[0][2] * pz - px);
					const double ny = (cq[1] - cp[1]) + kJawTurn * (rot.m[1][0] * px + rot.m[1][1] * py + rot.m[1][2] * pz - py);
					const double nz = (cq[2] - cp[2]) + kJawTurn * (rot.m[2][0] * px + rot.m[2][1] * py + rot.m[2][2] * pz - pz);
					out[k] = static_cast<float>(w * nx);
					out[k + 1] = static_cast<float>(w * ny);
					out[k + 2] = static_cast<float>(w * nz);
					if (std::fabs(nx) + std::fabs(ny) + std::fabs(nz) > 1e-6) {
						any = true;
					}
				}
				if (any) {
					target.morphs[name] = std::move(out);
					++carried;
				}
			}
			if (log) {
				int heavy = 0;
				for (const float w : wJaw) {
					heavy += w > 0.5f ? 1 : 0;
				}
				log->push_back(std::format("mouth part rigid with the jaw: {} of {} vertices are lower jaw (weight > 0.5), {} head vertices fitted, {} morphs carried over: {}", heavy, tn, jaw.size(), carried,
					target.rels.empty() ? std::string{} : target.rels.front()));
			}
		}
	}

	std::vector<Header> Headers(const Def& def, const Resolver& resolve)
	{
		std::vector<Header> out;
		for (const char* rel : def.files) {
			const std::string full = resolve(rel);
			int               nv = 0;
			if (!full.empty() && Tri::ReadVertexCount(full, nv)) {
				out.push_back({ nv, full });
			}
		}
		return out;
	}

	std::vector<Part> Load(const Def& def, const Resolver& resolve, std::vector<std::string>* log)
	{
		const auto note = [&](std::string s) {
			if (log) {
				log->push_back(std::move(s));
			}
		};
		std::vector<Part> parts;
		for (const char* rel : def.files) {
			const std::string full = resolve(rel);
			if (full.empty()) {
				continue;
			}
			Tri::File   f;
			std::string err;
			if (!Tri::Read(full, f, err)) {
				note(std::format("{}: cannot use {} ({})", def.name, full, err));
				continue;
			}
			if (!err.empty()) {
				note(std::format("{}: {} ({})", def.name, full, err));
			}
			Part* into = nullptr;
			for (auto& p : parts) {
				if (p.nv == f.nv && MeanDistance(p.base, f.base) < 0.5) {
					into = &p;
					break;
				}
			}
			if (!into) {
				parts.emplace_back();
				into = &parts.back();
				into->nv = f.nv;
				into->base = std::move(f.base);
				for (const auto& other : parts) {  // a second mesh with the same count: worth knowing about
					if (&other != into && other.nv == into->nv) {
						note(std::format("{}: {} has the same vertex count ({}) as {} but another shape: kept as a separate part", def.name, full, f.nv, other.files.front()));
						break;
					}
				}
			}
			into->files.push_back(full);
			into->rels.push_back(rel);
			int added = 0;
			for (auto& [name, dl] : f.morphs) {
				if (into->morphs.try_emplace(name, std::move(dl)).second) {
					++added;
				}
			}
			note(std::format("{}: {} -> {} vertices, {} morph(s), {} new", def.name, full, f.nv, f.morphs.size(), added));
		}
		// UBE's eyeball is a shallow CAP (measured: y 5.0..6.97, sphere centre y 5.85, radius 1.16; its centroid sits 0.52 in front of the centre). So like an add-on eye
		// pack it must turn RIGIDLY about the fitted sphere centre: the old per-vertex mask (from UBE's tiny shipped morphs) bent the cap and pivoted about the centroid,
		// which tilted the eye out of its socket and made the sideways look cross-eyed. The pivot is fitted once on eyesfemale.tri and shared by every UBE eye part.
		PivotHint ubeHint;
		for (auto& p : parts) {
			for (const auto& fl : p.rels) {
				const std::string lf = "/" + Lower(fl);
				if (lf.find("/!ube/eyes/eyesfemale.tri") != std::string::npos || lf.find("/!ube/eyes/eyesmale.tri") != std::string::npos) {
					std::vector<int> es(static_cast<std::size_t>(p.nv), 0);
					for (int i = 0; i < p.nv; ++i) {
						const float x = p.base[static_cast<std::size_t>(i) * 3];
						es[static_cast<std::size_t>(i)] = x < -0.01f ? -1 : (x > 0.01f ? 1 : 0);
					}
					bool ok = true;
					for (int k = 0; k < 2; ++k) {
						ok = ok && FitSphereCentre(p, es, k == 0 ? -1 : 1, ubeHint.c[k][0], ubeHint.c[k][1], ubeHint.c[k][2]);
					}
					ubeHint.has = ok;
				}
			}
		}
		for (auto& p : parts) {
			bool eyeFile = false;
			for (const auto& fl : p.rels) {
				if (Lower(fl).find("eyes") != std::string::npos) {
					eyeFile = true;
				}
			}
			bool ube = false;
			bool shell = false;  // the eye SHADOW and the wet rim lie on the lids, they are not eyeballs: they keep the shipped morphs and never turn with the eye (a turned shadow's dark edge slid over the iris)
			for (const auto& fl : p.rels) {
				ube = ube || IsUbeEyePath(fl);
				shell = shell || Has(fl, "shadow") || Has(fl, "wetness");
			}
			if (ube && shell) {
				AddEyeMorphs(p, false);
			} else if (ube) {
				p.ube = true;
				AddEyeMorphs(p, eyeFile, 1.0f, 1.0f, ubeHint.has && eyeFile, ubeHint.has ? &ubeHint : nullptr, &kUbeUnit);
			} else {
				AddEyeMorphs(p, eyeFile);
			}
		}
		if (def.followHead && parts.size() > 1) {
			// parts[0] is the head (its file is first in the list)
			for (std::size_t i = 1; i < parts.size(); ++i) {
				if (!IsFollower(parts[i])) {
					continue;
				}
				bool mouth = false;
				for (const auto& r : parts[i].rels) {
					mouth = mouth || Has(r, "mouth");
				}
				if (mouth) {
					FollowJaw(parts[0], parts[i], log);  // teeth and tongue are rigid
				} else {
					FollowHead(parts[0], parts[i], log);  // brows, lashes, eye shadows lie on the skin
				}
			}
		}
		return parts;
	}

	bool LoadFile(const std::string& fullPath, Part& out, std::string& err)
	{
		Tri::File f;
		if (!Tri::Read(fullPath, f, err)) {
			return false;
		}
		out = Part{};
		out.nv = f.nv;
		out.base = std::move(f.base);
		out.files.push_back(fullPath);
		out.rels.push_back(fullPath);
		for (auto& [name, dl] : f.morphs) {
			out.morphs.try_emplace(name, std::move(dl));
		}
		// Eye shapes of an add-on eye pack (Advanced Eyes Addon: EXPR_LDD_Eye_Inner / Outer / Shadow / lens files). Their shipped Look morphs move the eye less than the
		// vanilla ones (~75% sideways, ~60% up/down), so the per-eye look is multiplied by a gain, and they get the rotation morphs like a vanilla eye file.
		// Gain default 2.0; Data/SKSE/Plugins/FaceLab/EyePackGain.txt (one number) overrides it for tuning.
		const std::string low = Lower(fullPath);
		const bool        eyePack = low.find("_eye_") != std::string::npos && low.find("caruncula") == std::string::npos;
		static const std::pair<float, float> packGain = []() {
			float         g = 2.0f, gd = 3.0f;
			std::ifstream f("Data/SKSE/Plugins/FaceLab/EyePackGain.txt");  // "2.0" or "2.0 3.0": sideways+up, then down
			float         v = 0.0f, w = 0.0f;
			if (f && (f >> v) && v >= 0.1f && v <= 6.0f) {
				g = v;
				gd = (f >> w) && w >= 0.1f && w <= 6.0f ? w : v * 1.5f;
			}
			return std::make_pair(g, gd);
		}();
		PivotHint hint;
		if (eyePack && (low.find("_lensl") != std::string::npos || low.find("_lensr") != std::string::npos)) {
			// the matching eyeball file (the same name without _LensL / _LensR): its sphere centre is the pivot of both
			std::string inner = fullPath;
			for (const char* tag : { "_LensL", "_LensR", "_lensl", "_lensr" }) {
				if (const auto at = inner.find(tag); at != std::string::npos) {
					inner.erase(at, 6);
					break;
				}
			}
			Tri::File ef;
			std::string e2;
			if (inner != fullPath && Tri::Read(inner, ef, e2) && ef.base.size() == static_cast<std::size_t>(ef.nv) * 3) {
				Part         ep;
				ep.nv = ef.nv;
				ep.base = ef.base;
				std::vector<int> es(static_cast<std::size_t>(ep.nv), 0);
				for (int i = 0; i < ep.nv; ++i) {
					const float x = ep.base[static_cast<std::size_t>(i) * 3];
					es[static_cast<std::size_t>(i)] = x < -0.01f ? -1 : (x > 0.01f ? 1 : 0);
				}
				bool ok = true;
				for (int k = 0; k < 2; ++k) {
					ok = ok && FitSphereCentre(ep, es, k == 0 ? -1 : 1, hint.c[k][0], hint.c[k][1], hint.c[k][2]);
				}
				hint.has = ok;
			}
		}
		const bool ubeEye = IsUbeEyePath(low);
		out.ube = ubeEye && !eyePack;
		AddEyeMorphs(out, low.find("eyes") != std::string::npos || eyePack, eyePack ? packGain.first : 1.0f, eyePack ? packGain.second : 1.0f, eyePack, hint.has ? &hint : nullptr, ubeEye && !eyePack ? &kUbeUnit : nullptr);
		if (eyePack) {
			// Measured on the AEA eyeball (a cap 0.84 deep, rim radius 1.15, sphere radius about 1.2): one unit of the shipped look morph turns it about 14 degrees sideways and
			// 5 degrees up or down. Past roughly 30 degrees the cap's open edge shows in the lid opening. MaxAngle (degrees, default 28) can be set in
			// Data/SKSE/Plugins/FaceLab/EyePackMaxAngle.txt (one number, 10 to 60).
			static const float maxDeg = []() {
				float         v = 28.0f, x = 0.0f;
				std::ifstream f("Data/SKSE/Plugins/FaceLab/EyePackMaxAngle.txt");
				if (f && (f >> x) && x >= 10.0f && x <= 60.0f) {
					v = x;
				}
				return v;
			}();
			out.eyeWMax[0] = maxDeg / (14.0f * packGain.first);
			out.eyeWMax[1] = maxDeg / (5.0f * packGain.first);
			out.eyeWMax[2] = maxDeg / (5.0f * packGain.second);
			out.lookDeg[0] = 10.0f * packGain.first * 0.25f;
			out.lookDeg[1] = 10.0f * packGain.second * 0.5f;
		}
		return !out.morphs.empty();
	}
}
