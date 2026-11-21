/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */
/* Adapted for OpenSillicon from Linux v6.12; see README.md. */
#include "kernel/nouveau.h"

bool
nouveau_identify(uint32_t boot0, struct nouveau_identity *identity)
{
	if (identity == NULL)
		return false;
	*identity = (struct nouveau_identity){ .boot0 = boot0 };
	if (boot0 == 0 || boot0 == UINT32_MAX)
		return false;
	/* determine chipset and derive architecture from it */
	if ((boot0 & 0x1f000000) > 0) {
		identity->chipset = (boot0 & 0x1ff00000) >> 20;
		identity->chiprev = (boot0 & 0x000000ff);
		switch (identity->chipset & 0x1f0) {
		case 0x010: {
			if (0x461 & (1 << (identity->chipset & 0xf)))
				identity->card_type = NV_10;
			else
				identity->card_type = NV_11;
			identity->chiprev = 0x00;
			break;
		}
		case 0x020: identity->card_type = NV_20; break;
		case 0x030: identity->card_type = NV_30; break;
		case 0x040:
		case 0x060: identity->card_type = NV_40; break;
		case 0x050:
		case 0x080:
		case 0x090:
		case 0x0a0: identity->card_type = NV_50; break;
		case 0x0c0:
		case 0x0d0: identity->card_type = NV_C0; break;
		case 0x0e0:
		case 0x0f0:
		case 0x100: identity->card_type = NV_E0; break;
		case 0x110:
		case 0x120: identity->card_type = GM100; break;
		case 0x130: identity->card_type = GP100; break;
		case 0x140: identity->card_type = GV100; break;
		case 0x160: identity->card_type = TU100; break;
		case 0x170: identity->card_type = GA100; break;
		case 0x190: identity->card_type = AD100; break;
		default:
			break;
		}
	} else
	if ((boot0 & 0xff00fff0) == 0x20004000) {
		if (boot0 & 0x00f00000)
			identity->chipset = 0x05;
		else
			identity->chipset = 0x04;
		identity->card_type = NV_04;
	}

	switch (identity->chipset) {
	case 0x004: identity->name = "NV04"; break;
	case 0x005: identity->name = "NV05"; break;
	case 0x010: identity->name = "NV10"; break;
	case 0x011: identity->name = "NV11"; break;
	case 0x015: identity->name = "NV15"; break;
	case 0x017: identity->name = "NV17"; break;
	case 0x018: identity->name = "NV18"; break;
	case 0x01a: identity->name = "nForce"; break;
	case 0x01f: identity->name = "nForce2"; break;
	case 0x020: identity->name = "NV20"; break;
	case 0x025: identity->name = "NV25"; break;
	case 0x028: identity->name = "NV28"; break;
	case 0x02a: identity->name = "NV2A"; break;
	case 0x030: identity->name = "NV30"; break;
	case 0x031: identity->name = "NV31"; break;
	case 0x034: identity->name = "NV34"; break;
	case 0x035: identity->name = "NV35"; break;
	case 0x036: identity->name = "NV36"; break;
	case 0x040: identity->name = "NV40"; break;
	case 0x041: identity->name = "NV41"; break;
	case 0x042: identity->name = "NV42"; break;
	case 0x043: identity->name = "NV43"; break;
	case 0x044: identity->name = "NV44"; break;
	case 0x045: identity->name = "NV45"; break;
	case 0x046: identity->name = "G72"; break;
	case 0x047: identity->name = "G70"; break;
	case 0x049: identity->name = "G71"; break;
	case 0x04a: identity->name = "NV44A"; break;
	case 0x04b: identity->name = "G73"; break;
	case 0x04c: identity->name = "C61"; break;
	case 0x04e: identity->name = "C51"; break;
	case 0x050: identity->name = "G80"; break;
	case 0x063: identity->name = "C73"; break;
	case 0x067: identity->name = "C67"; break;
	case 0x068: identity->name = "C68"; break;
	case 0x084: identity->name = "G84"; break;
	case 0x086: identity->name = "G86"; break;
	case 0x092: identity->name = "G92"; break;
	case 0x094: identity->name = "G94"; break;
	case 0x096: identity->name = "G96"; break;
	case 0x098: identity->name = "G98"; break;
	case 0x0a0: identity->name = "GT200"; break;
	case 0x0a3: identity->name = "GT215"; break;
	case 0x0a5: identity->name = "GT216"; break;
	case 0x0a8: identity->name = "GT218"; break;
	case 0x0aa: identity->name = "MCP77/MCP78"; break;
	case 0x0ac: identity->name = "MCP79/MCP7A"; break;
	case 0x0af: identity->name = "MCP89"; break;
	case 0x0c0: identity->name = "GF100"; break;
	case 0x0c1: identity->name = "GF108"; break;
	case 0x0c3: identity->name = "GF106"; break;
	case 0x0c4: identity->name = "GF104"; break;
	case 0x0c8: identity->name = "GF110"; break;
	case 0x0ce: identity->name = "GF114"; break;
	case 0x0cf: identity->name = "GF116"; break;
	case 0x0d7: identity->name = "GF117"; break;
	case 0x0d9: identity->name = "GF119"; break;
	case 0x0e4: identity->name = "GK104"; break;
	case 0x0e6: identity->name = "GK106"; break;
	case 0x0e7: identity->name = "GK107"; break;
	case 0x0ea: identity->name = "GK20A"; break;
	case 0x0f0: identity->name = "GK110"; break;
	case 0x0f1: identity->name = "GK110B"; break;
	case 0x106: identity->name = "GK208B"; break;
	case 0x108: identity->name = "GK208"; break;
	case 0x117: identity->name = "GM107"; break;
	case 0x118: identity->name = "GM108"; break;
	case 0x120: identity->name = "GM200"; break;
	case 0x124: identity->name = "GM204"; break;
	case 0x126: identity->name = "GM206"; break;
	case 0x12b: identity->name = "GM20B"; break;
	case 0x130: identity->name = "GP100"; break;
	case 0x132: identity->name = "GP102"; break;
	case 0x134: identity->name = "GP104"; break;
	case 0x136: identity->name = "GP106"; break;
	case 0x137: identity->name = "GP107"; break;
	case 0x138: identity->name = "GP108"; break;
	case 0x13b: identity->name = "GP10B"; break;
	case 0x140: identity->name = "GV100"; break;
	case 0x162: identity->name = "TU102"; break;
	case 0x164: identity->name = "TU104"; break;
	case 0x166: identity->name = "TU106"; break;
	case 0x167: identity->name = "TU117"; break;
	case 0x168: identity->name = "TU116"; break;
	case 0x172: identity->name = "GA102"; break;
	case 0x173: identity->name = "GA103"; break;
	case 0x174: identity->name = "GA104"; break;
	case 0x176: identity->name = "GA106"; break;
	case 0x177: identity->name = "GA107"; break;
	case 0x192: identity->name = "AD102"; break;
	case 0x193: identity->name = "AD103"; break;
	case 0x194: identity->name = "AD104"; break;
	case 0x196: identity->name = "AD106"; break;
	case 0x197: identity->name = "AD107"; break;
	default: return false;
	}
	return identity->card_type != 0;
}

uint32_t
nouveau_crystal_khz(const struct nouveau_identity *identity, uint32_t strap)
{
	if (identity == NULL || identity->name == NULL)
		return 0;
	if (identity->card_type <= NV_10 || identity->chipset < 0x17 ||
	    (identity->chipset >= 0x20 && identity->chipset < 0x25))
		strap &= 0x00000040;
	else
		strap &= 0x00400040;
	switch (strap) {
	case 0x00000000: return 13500;
	case 0x00000040: return 14318;
	case 0x00400000: return 27000;
	case 0x00400040: return 25000;
	}
	return 0;
}
