// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - FaceLab. See LICENSE.

#include "Families.h"

#include <map>

namespace FaceLab::Fam
{
	namespace
	{
		// "facegenmorphs/morphs/" prefix is spelled out: the shipped files live in several trees (facegenmorphs, character assets, !UBE ...)
		// ---- High Poly Head (KL) ----
		const std::vector<const char*> kHphFemale = {
			"actors/character/facegenmorphs/morphs/KL/femalehead_expr.tri",
			"actors/character/facegenmorphs/morphs/expressions/mouth/expr_mouthhumanf.tri",
			"actors/character/facegenmorphs/morphs/expressions/expr_eyesfemale.tri",
			"actors/character/facegenmorphs/morphs/KL/faceparts/femaleheadbrows_expr.tri",
			"actors/character/facegenmorphs/morphs/KL/faceparts/femaleheadmaskleftside_expr.tri",
			"actors/character/facegenmorphs/morphs/KL/faceparts/femaleheadmaskrightside_expr.tri",
		};
		const std::vector<const char*> kHphMale = {
			"actors/character/facegenmorphs/morphs/KL/malehead_expr.tri",
			"actors/character/facegenmorphs/morphs/expressions/mouth/expr_mouthhuman.tri",
			"actors/character/facegenmorphs/morphs/expressions/expr_eyesmale.tri",
			"actors/character/facegenmorphs/morphs/KL/faceparts/maleheadbrows_expr.tri",
			"actors/character/facegenmorphs/morphs/KL/faceparts/maleheadmaskleftside_expr.tri",
			"actors/character/facegenmorphs/morphs/KL/faceparts/maleheadmaskrightside_expr.tri",
		};
		// ---- Vanilla head, with Expressive Facial Animation (EFA): the plain morphs of "character assets" (loose files: the game's own copies are inside its BSA, which FaceLab cannot read), plus the 996 / 898 vertex EXPR files
		//      of the High Poly Head / Waifu Expression Redux packs when they are installed (more names) ----
		const std::vector<const char*> kVanillaFemale = {
			"actors/character/character assets/FemaleHead.tri",
			"actors/character/facegenmorphs/morphs/expressions/expr_femalehead.tri",
			"actors/character/character assets/EyesFemale.tri",
			"actors/character/facegenmorphs/morphs/expressions/expr_eyesfemale.tri",
			"actors/character/character assets/faceparts/EyesFemaleLeft.tri",
			"actors/character/facegenmorphs/morphs/expressions/faceparts/expr_eyesfemaleleft.tri",
			"actors/character/character assets/faceparts/EyesFemaleRight.tri",
			"actors/character/facegenmorphs/morphs/expressions/faceparts/expr_eyesfemaleright.tri",
			"actors/character/character assets/faceparts/FemaleHeadBrows.tri",
			"actors/character/facegenmorphs/morphs/expressions/faceparts/expr_femaleheadbrows.tri",
			"actors/character/character assets/faceparts/FemaleHeadMaskLeftside.tri",
			"actors/character/facegenmorphs/morphs/expressions/faceparts/expr_femaleheadmaskleftside.tri",
			"actors/character/character assets/faceparts/FemaleHeadMaskRightside.tri",
			"actors/character/facegenmorphs/morphs/expressions/faceparts/expr_femaleheadmaskrightside.tri",
			"actors/character/character assets/mouth/MouthHumanF.tri",
			"actors/character/facegenmorphs/morphs/expressions/mouth/expr_mouthhumanf.tri",
			"actors/character/character assets/mouth/MouthOrcFemale.tri",
			"actors/character/facegenmorphs/morphs/expressions/mouth/expr_mouthorcfemale.tri",
		};
		const std::vector<const char*> kVanillaMale = {
			"actors/character/character assets/MaleHead.tri",
			"actors/character/facegenmorphs/morphs/expressions/expr_malehead.tri",
			"actors/character/character assets/EyesMale.tri",
			"actors/character/facegenmorphs/morphs/expressions/expr_eyesmale.tri",
			"actors/character/character assets/faceparts/EyesMaleLeft.tri",
			"actors/character/facegenmorphs/morphs/expressions/faceparts/expr_eyesmaleleft.tri",
			"actors/character/character assets/faceparts/EyesMaleRight.tri",
			"actors/character/facegenmorphs/morphs/expressions/faceparts/expr_eyesmaleright.tri",
			"actors/character/character assets/faceparts/MaleHeadbrows.tri",
			"actors/character/facegenmorphs/morphs/expressions/faceparts/expr_maleheadbrows.tri",
			"actors/character/character assets/faceparts/MaleHeadmaskLeftside.tri",
			"actors/character/facegenmorphs/morphs/expressions/faceparts/expr_maleheadmaskleftside.tri",
			"actors/character/character assets/faceparts/MaleHeadmaskRightside.tri",
			"actors/character/facegenmorphs/morphs/expressions/faceparts/expr_maleheadmaskrightside.tri",
			"actors/character/character assets/mouth/Mouthhuman.tri",
			"actors/character/facegenmorphs/morphs/expressions/mouth/expr_mouthhuman.tri",
			"actors/character/character assets/mouth/MouthOrc.tri",
			"actors/character/facegenmorphs/morphs/expressions/mouth/expr_mouthorc.tri",
		};
		// ---- CoTR (Trixter = female, Genesis = male). The high poly pack (12206) uses the same file names but is NOT supported (Morph.cpp ignores a head file that large):
		//      only the low poly head (3127 vertices) is driven ----
		const std::vector<const char*> kCotrFemale = {
			"actors/character/facegenmorphs/morphs/Trixter/EXPR_FemaleHead.tri",
			"actors/character/facegenmorphs/morphs/Genesis/Mouth/EXPR_MouthHumanF.tri",
			"actors/character/facegenmorphs/morphs/Genesis/Eyes/EXPR_EyesFemale.tri",
			"actors/character/facegenmorphs/morphs/Genesis/Eyes/EXPR_LensFemale.tri",
			"actors/character/facegenmorphs/morphs/Genesis/faceparts/EXPR_EyeFemaleLashes.tri",
			"actors/character/facegenmorphs/morphs/Genesis/faceparts/EXPR_EyeFemaleShadow.tri",
			"actors/character/facegenmorphs/morphs/Genesis/faceparts/EXPR_FemaleBrows.tri",
			"actors/character/facegenmorphs/morphs/Genesis/faceparts/EXPR_FemaleMask.tri",
		};
		const std::vector<const char*> kCotrMale = {
			"actors/character/facegenmorphs/morphs/Genesis/EXPR_MaleHead.tri",
			"actors/character/facegenmorphs/morphs/Genesis/Mouth/EXPR_MouthM.tri",
			"actors/character/facegenmorphs/morphs/Genesis/Eyes/EXPR_EyesMale.tri",
			"actors/character/facegenmorphs/morphs/Genesis/Eyes/EXPR_LensMale.tri",
			"actors/character/facegenmorphs/morphs/Genesis/faceparts/EXPR_EyeMaleLashes.tri",
			"actors/character/facegenmorphs/morphs/Genesis/faceparts/EXPR_EyeMaleShadow.tri",
			"actors/character/facegenmorphs/morphs/Genesis/faceparts/EXPR_MaleBrows.tri",
			"actors/character/facegenmorphs/morphs/Genesis/faceparts/EXPR_MaleMask.tri",
		};
		// ---- UBE (female only). The head has a rich EXPR file ("John"); the face parts only have the plain morphs of their own tri and a small LRO EXPR set ----
		const std::vector<const char*> kUbeFemale = {
			"actors/character/facegenmorphs/morphs/John/EXPR_FemaleHead.tri",
			"!UBE/Head/FemaleHead.tri",
			"actors/character/facegenmorphs/morphs/LRO/EXPR2/FemaleHead.tri",
			"!UBE/Mouth/mouthhumanf_tangent.tri",
			"actors/character/facegenmorphs/morphs/LRO/EXPR2/MouthHumanF.tri",
			"!UBE/Eyes/eyesfemale.tri",
			"actors/character/facegenmorphs/morphs/LRO/EXPR2/Eyesfemale.tri",
			"!UBE/Eyes/eyesfemale_outer.tri",
			"actors/character/facegenmorphs/morphs/LRO/EXPR2/Eyesfemale_outer.tri",
			"!UBE/Eyes/LensFemale.tri",
			"actors/character/facegenmorphs/morphs/LRO/EXPR2/LensFemale.tri",
			"!UBE/Eyes/LensFemaleLeft.tri",
			"actors/character/facegenmorphs/morphs/LRO/EXPR2/LenseFemaleLeft.tri",
			"!UBE/Eyes/LensFemaleRight.tri",
			"actors/character/facegenmorphs/morphs/LRO/EXPR2/LenseFemaleRight.tri",
			"!UBE/Eyes/eyesfemale_shadows.tri",
			"actors/character/facegenmorphs/morphs/LRO/EXPR2/eyesfemale_shadows.tri",
			"!UBE/Eyes/eyesfemale_wetness.tri",
			"actors/character/facegenmorphs/morphs/LRO/EXPR2/eyeswetness.tri",
			"!UBE/Lashes/EyeFemaleLashes.tri",
			"actors/character/facegenmorphs/morphs/LRO/EXPR2/EyeFemaleLashes.tri",
			"!UBE/Brows/femaleheadbrows.tri",
			"actors/character/facegenmorphs/morphs/LRO/EXPR2/FemaleBrows.tri",
		};
	}

	const std::vector<Def>& All()
	{
		// Order = priority when two families fit equally well (the High Poly Head first: it is what FaceLab always supported)
		static const std::vector<Def> all = {
			{ "High Poly Head (female)", true, kHphFemale },
			{ "High Poly Head (male)", false, kHphMale },
			{ "CoTR (female)", true, kCotrFemale },
			{ "CoTR (male)", false, kCotrMale },
			{ "UBE (female)", true, kUbeFemale, true },
			{ "Vanilla (female)", true, kVanillaFemale },
			{ "Vanilla (male)", false, kVanillaMale },
		};
		return all;
	}

	const std::vector<const char*>& Aliases(const std::string& name)
	{
		static const std::map<std::string, std::vector<const char*>> a = {
			// mouth shapes the vanilla and UBE heads do not have (their tris hold Aah BigAah BMP ChJSh DST Eee OohQ): the nearest one that exists
			{ "Oh", { "OohQ" } },
			{ "W", { "OohQ" } },
			{ "R", { "OohQ" } },
			{ "I", { "Eee" } },
			{ "Eh", { "Eee", "Aah" } },
			{ "K", { "Aah" } },
			{ "N", { "DST" } },
			{ "Th", { "DST" } },
			{ "FV", { "DST", "BMP" } },
			// spelling differences between heads
			{ "MoodSurprise", { "MoodSurprised" } },
			{ "MoodSurprised", { "MoodSurprise" } },
			{ "DialogueSurprise", { "DialogueSurprised" } },
			{ "DialogueSurprised", { "DialogueSurprise" } },
		};
		static const std::vector<const char*> none;
		const auto                            it = a.find(name);
		return it == a.end() ? none : it->second;
	}
}
