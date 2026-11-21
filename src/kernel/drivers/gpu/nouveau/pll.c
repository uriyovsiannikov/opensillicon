/*
 * Copyright 2013 Red Hat Inc.
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
/*
 * Copyright 2005-2006 Erik Waling
 * Copyright 2006 Stephane Marchesin
 * Copyright 2007-2009 Stuart Bennett
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
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
/*
 * Copyright 2010 Red Hat Inc.
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
/* gt215_pll_calc fractional path and gf100_devinit_pll_set sequence,
 * Linux v6.12. Native checked arithmetic, no register writes here. */
#include "kernel/nouveau_pll.h"
static uint16_t u16(const uint8_t *p) {return p[0]|((uint16_t)p[1]<<8);}
static bool valid(const struct nouveau_pll_limits *l)
{
	/* All clock quantities are kHz; reject bounds exceeding the BIOS
	 * 16-bit MHz encoding as well as zero divisors and reversed limits. */
	return l && l->ref_khz<=65535000 && l->vco_min_khz &&
	    l->vco_max_khz>=l->vco_min_khz && l->vco_max_khz<=65535000 &&
	    l->input_min_khz && l->input_max_khz>=l->input_min_khz &&
	    l->input_max_khz<=65535000 && l->max_m && l->min_m<=l->max_m &&
	    l->min_n<=l->max_n && l->max_p && l->min_p<=l->max_p;
}
bool nouveau_pll_limits_v40(const uint8_t *payload,size_t size,uint32_t ref,
	struct nouveau_pll_limits *limits)
{
	if(!payload || size<14 || !limits) return false;
	struct nouveau_pll_limits l={.ref_khz=ref,
	    .vco_min_khz=(uint32_t)u16(payload)*1000,.vco_max_khz=(uint32_t)u16(payload+2)*1000,
	    .input_min_khz=(uint32_t)u16(payload+4)*1000,.input_max_khz=(uint32_t)u16(payload+6)*1000,
	    .min_m=payload[8],.max_m=payload[9],.min_n=payload[10],.max_n=payload[11],
	    .min_p=payload[12],.max_p=payload[13]};
	if(!valid(&l)) return false;
	*limits=l;return true;
}
bool nouveau_pll_calculate(const struct nouveau_pll_limits *l,uint32_t target,
	struct nouveau_pll_coefficients *result)
{
	if(!result) return false;
	*result=(struct nouveau_pll_coefficients){0};
	if(!valid(l) || !l->ref_khz || !target || target>l->vco_max_khz) return false;
	uint32_t p=l->vco_max_khz/target;
	if(p>l->max_p)p=l->max_p;
	if(p<l->min_p)p=l->min_p;
	uint64_t vco=(uint64_t)target*p;
	if(vco<l->vco_min_khz || vco>l->vco_max_khz) return false;
	uint32_t low=(l->ref_khz+l->input_max_khz)/l->input_max_khz;
	uint32_t high=(l->ref_khz+l->input_min_khz)/l->input_min_khz;
	if(low<l->min_m)low=l->min_m;
	if(high>l->max_m)high=l->max_m;
	if(low>high)low=high;
	for(uint32_t m=low;m<=high;m++) {
		if(!m || m<l->min_m || (uint64_t)l->input_min_khz*m>l->ref_khz ||
		    (uint64_t)l->input_max_khz*m<l->ref_khz) continue;
		uint64_t tmp=vco*m,n=tmp/l->ref_khz,remainder=tmp%l->ref_khz;
		if(remainder<l->ref_khz/2) {if(!n)continue;n--;}
		if(n<l->min_n || n>l->max_n) continue;
		uint64_t fraction=((tmp-n*l->ref_khz)*8192+l->ref_khz/2)/l->ref_khz;
		int64_t signed_fraction=(int64_t)fraction-4096;
		if(signed_fraction<INT16_MIN || signed_fraction>INT16_MAX) continue;
		uint64_t numerator=(uint64_t)l->ref_khz*(n*8192+4096+signed_fraction);
		uint64_t denominator=(uint64_t)8192*m*p;
		uint32_t actual=(uint32_t)((numerator+denominator/2)/denominator);
		if(!actual) continue;
		*result=(struct nouveau_pll_coefficients){.m=m,.n=n,.p=p,
		    .fraction=(uint16_t)signed_fraction,.actual_khz=actual,
		    .coefficient_word=(p<<16)|((uint32_t)n<<8)|m,
		    .fraction_word=(uint32_t)(uint16_t)signed_fraction<<16};
		return true;
	}
	return false;
}
bool nouveau_pll_register_valid(unsigned head,uint32_t reg)
{
	return head<4 && (reg==0x614100+head*0x800 || reg==0x614140+head*0x800);
}
bool nouveau_pll_plan(unsigned head,uint32_t reg,uint32_t control,
	const struct nouveau_pll_coefficients *c,struct nouveau_pll_plan *plan)
{
	if(!c || !plan || !nouveau_pll_register_valid(head,reg) || control==UINT32_MAX ||
	    !c->m || !c->p || !c->actual_khz ||
	    c->coefficient_word!=((uint32_t)c->p<<16|((uint32_t)c->n<<8)|c->m) ||
	    c->fraction_word!=((uint32_t)c->fraction<<16)) return false;
	*plan=(struct nouveau_pll_plan){.writes={{reg+12,control|0x100},
	    {reg+4,c->coefficient_word},{reg+16,c->fraction_word}}};
	return true;
}
