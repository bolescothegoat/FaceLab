// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - FaceLab. See LICENSE.

#include "TriFile.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace FaceLab::Tri
{
	namespace
	{
		constexpr std::size_t kHeader = 64;

		std::int32_t I32(const std::vector<char>& d, std::size_t at)
		{
			std::int32_t v = 0;
			std::memcpy(&v, d.data() + at, 4);
			return v;
		}

		// Is there a morph record at `pos`? Returns the position after it (0 = no record here).
		std::size_t RecordAt(const std::vector<char>& d, std::size_t pos, std::size_t need, std::string* nameOut, float* scaleOut)
		{
			if (pos + 8 > d.size()) {
				return 0;
			}
			const std::int32_t len = I32(d, pos);
			if (len < 2 || len > 64 || pos + 4 + static_cast<std::size_t>(len) + 4 + need > d.size()) {
				return 0;
			}
			const char* nm = d.data() + pos + 4;
			if (nm[len - 1] != 0) {
				return 0;
			}
			for (int i = 0; i < len - 1; ++i) {
				const unsigned char c = static_cast<unsigned char>(nm[i]);
				if (c < 0x20 || c > 0x7e) {
					return 0;
				}
			}
			float scale = 0.0f;
			std::memcpy(&scale, d.data() + pos + 4 + len, 4);
			if (!std::isfinite(scale) || scale < 0.0f || scale > 1000.0f) {  // 0 is valid: a morph with no movement
				return 0;
			}
			if (nameOut) {
				nameOut->assign(nm, static_cast<std::size_t>(len) - 1);
			}
			if (scaleOut) {
				*scaleOut = scale;
			}
			return pos + 4 + static_cast<std::size_t>(len) + 4 + need;
		}
	}

	bool ReadVertexCount(const std::string& path, int& nv)
	{
		std::ifstream f(path, std::ios::binary);
		char          h[12]{};
		if (!f || !f.read(h, 12) || std::memcmp(h, "FRTRI003", 8) != 0) {
			return false;
		}
		std::int32_t n = 0;
		std::memcpy(&n, h + 8, 4);
		if (n <= 0) {
			return false;
		}
		nv = n;
		return true;
	}

	std::string PlainName(const std::string& s)
	{
		// "EXPR_" (High Poly Head, CoTR female) or "EXPR1_" / "EXPR2_" ... (CoTR male): a prefix of EXPR, optional digits, an underscore
		if (s.size() > 5 && (s[0] == 'E' || s[0] == 'e') && (s[1] == 'X' || s[1] == 'x') && (s[2] == 'P' || s[2] == 'p') && (s[3] == 'R' || s[3] == 'r')) {
			std::size_t i = 4;
			while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
				++i;
			}
			if (i < s.size() - 1 && s[i] == '_') {
				return s.substr(i + 1);
			}
		}
		return s;
	}

	bool Read(const std::string& path, File& out, std::string& err)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f) {
			err = "cannot open";
			return false;
		}
		std::vector<char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		if (d.size() < kHeader || std::memcmp(d.data(), "FRTRI003", 8) != 0) {
			err = "not a FRTRI003 file";
			return false;
		}
		const std::int32_t nv = I32(d, 8);
		if (nv <= 0 || nv > 200000) {
			err = "implausible vertex count";
			return false;
		}
		out = File{};
		out.nv = nv;
		out.path = path;
		const std::size_t need = static_cast<std::size_t>(nv) * 6;
		if (d.size() >= kHeader + static_cast<std::size_t>(nv) * 12) {
			out.base.resize(static_cast<std::size_t>(nv) * 3);
			std::memcpy(out.base.data(), d.data() + kHeader, static_cast<std::size_t>(nv) * 12);
		}

		// Where the morph records start depends on the sections before them (triangles, quads, labels, UVs ...). Rather than trust every count in the header, look for
		// the first place where a run of valid records begins, and prefer the run that has exactly the number of records the header announces.
		const std::int32_t want = I32(d, 36);  // number of differential morphs
		std::size_t        from = kHeader + static_cast<std::size_t>(nv) * 12;
		const std::int32_t nt = I32(d, 12), nq = I32(d, 16);
		if (nt >= 0 && nq >= 0 && nt < 1000000 && nq < 1000000) {
			from += static_cast<std::size_t>(nt) * 12 + static_cast<std::size_t>(nq) * 16;  // the geometry is at least this big
		}
		std::size_t bestPos = 0;
		int         bestRun = 0;
		for (std::size_t p = from; p + 12 < d.size(); ++p) {
			std::size_t q = RecordAt(d, p, need, nullptr, nullptr);
			if (!q) {
				continue;
			}
			int run = 1;
			while (run < 4096) {
				const std::size_t n = RecordAt(d, q, need, nullptr, nullptr);
				if (!n) {
					break;
				}
				q = n;
				++run;
			}
			if (want > 0 && run == want) {
				bestPos = p;
				bestRun = run;
				break;
			}
			if (run > bestRun) {
				bestRun = run;
				bestPos = p;
			}
			if (run >= 3) {
				p = q - 1;  // a long run found: do not look for another start inside it
			}
		}
		if (!bestRun) {
			err = "no morph records found";
			return false;
		}
		std::size_t pos = bestPos;
		for (int i = 0; i < bestRun; ++i) {
			std::string name;
			float       scale = 0.0f;
			const std::size_t next = RecordAt(d, pos, need, &name, &scale);
			if (!next) {
				break;
			}
			std::vector<float> dl(static_cast<std::size_t>(nv) * 3);
			const char*        src = d.data() + (next - need);
			for (std::size_t k = 0; k < static_cast<std::size_t>(nv) * 3; ++k) {
				std::int16_t v = 0;
				std::memcpy(&v, src + k * 2, 2);
				dl[k] = static_cast<float>(v) * scale;
			}
			out.morphs.emplace_back(PlainName(name), std::move(dl));
			pos = next;
		}
		if (want > 0 && static_cast<int>(out.morphs.size()) != want) {
			err = "header announces " + std::to_string(want) + " morphs, found " + std::to_string(out.morphs.size());
			// not fatal: the records that are there are valid
		}
		return !out.morphs.empty();
	}
}
