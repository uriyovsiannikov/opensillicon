/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef KERNEL_NOUVEAU_PLL_H
#define KERNEL_NOUVEAU_PLL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct nouveau_pll_limits {
	uint32_t ref_khz,vco_min_khz,vco_max_khz,input_min_khz,input_max_khz;
	uint8_t min_m,max_m,min_n,max_n,min_p,max_p;
};
struct nouveau_pll_coefficients {
	uint8_t m,n,p;
	uint16_t fraction;
	uint32_t actual_khz,coefficient_word,fraction_word;
};
/* Decode v0x40 limits payload (14 bytes), with the refclk from its parent
 * entry. Caller must resolve/validate VBIOS pointers and select VPLL type.
 * Zero refclk denotes the VBIOS crystal fallback request; decoding permits
 * it, but calculate rejects it until caller supplies the identified crystal. */
bool nouveau_pll_limits_v40(const uint8_t *payload,size_t size,uint32_t ref_khz,
	struct nouveau_pll_limits *limits);
/* Fractional VPLL path of gt215_pll_calc, using checked 64-bit arithmetic. */
bool nouveau_pll_calculate(const struct nouveau_pll_limits *,uint32_t target_khz,
	struct nouveau_pll_coefficients *);
struct nouveau_pll_write { uint32_t reg,value; };
struct nouveau_pll_plan { struct nouveau_pll_write writes[3]; };
/* CPU-only GF100 VPLL programming sequence used by GP107 devinit. Caller
 * supplies the register base validated from VBIOS and current control value.
 * This does NOT program/lock a PLL or demonstrate the resulting pixel clock. */
bool nouveau_pll_plan(unsigned head,uint32_t bios_register,uint32_t saved_control,
	const struct nouveau_pll_coefficients *,struct nouveau_pll_plan *);
#endif
