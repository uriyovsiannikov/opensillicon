#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kernel/nouveau_pll.h"
static uint32_t seed=0x148500;
static uint32_t random32(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int main(void)
{
	/* Synthetic limits, not measured GTX 1050 Ti firmware limits. */
	const uint8_t payload[]={0xf4,1,0xb8,0x0b,1,0,27,0,1,32,1,255,1,63};
	struct nouveau_pll_limits l;
	assert(nouveau_pll_limits_v40(payload,sizeof(payload),27000,&l));
	assert(l.vco_min_khz==500000&&l.vco_max_khz==3000000&&l.ref_khz==27000);
	struct nouveau_pll_coefficients c;
	assert(nouveau_pll_calculate(&l,148500,&c));
	assert(c.m==2&&c.n==219&&c.p==20&&c.fraction==4096&&c.actual_khz==148500);
	assert(c.coefficient_word==0x14db02&&c.fraction_word==0x10000000);
	struct nouveau_pll_plan p;
	assert(nouveau_pll_plan(0,0x614100,0x20,&c,&p));
	const struct nouveau_pll_write expected[]={{0x61410c,0x120},{0x614104,0x14db02},{0x614110,0x10000000}};
	assert(!memcmp(p.writes,expected,sizeof(expected)));
	for(unsigned head=0;head<4;head++) {
		uint32_t reg=0x614140+head*0x800;
		assert(nouveau_pll_plan(head,reg,0x42,&c,&p));
		assert(p.writes[0].reg==reg+12&&p.writes[0].value==0x142);
		assert(p.writes[1].reg==reg+4&&p.writes[2].reg==reg+16);
		assert(!nouveau_pll_register_valid(head,reg+4));
		assert(!nouveau_pll_register_valid(head,reg+0x800));
	}
	assert(nouveau_pll_plan(3,0x615900,0,&c,&p)&&p.writes[0].reg==0x61590c);
	assert(!nouveau_pll_plan(4,0x616100,0,&c,&p));
	assert(!nouveau_pll_plan(0,0xdeadbeef,0,&c,&p));
	assert(!nouveau_pll_plan(0,0x614100,UINT32_MAX,&c,&p));
	assert(!nouveau_pll_calculate(&l,0,&c)&&!c.m);
	assert(!nouveau_pll_calculate(&l,UINT32_MAX,&c));
	assert(!nouveau_pll_calculate(&l,1,&c));
	for(unsigned i=0;i<14;i++)assert(!nouveau_pll_limits_v40(payload,i,27000,&l));
	assert(nouveau_pll_limits_v40(payload,14,0,&l)&&!l.ref_khz);
	assert(!nouveau_pll_calculate(&l,148500,&c));
	uint8_t zero_minima[14];memcpy(zero_minima,payload,14);
	zero_minima[8]=zero_minima[10]=zero_minima[12]=0;
	assert(nouveau_pll_limits_v40(zero_minima,14,27000,&l));
	assert(nouveau_pll_calculate(&l,148500,&c)&&c.m&&c.p&&c.actual_khz==148500);
	assert(nouveau_pll_plan(0,0x614100,0,&c,&p));
	l=(struct nouveau_pll_limits){.ref_khz=27000,.vco_min_khz=10000,.vco_max_khz=20000,
	    .input_min_khz=1000,.input_max_khz=27000,.min_m=0,.max_m=32,.min_n=0,.max_n=255,.min_p=0,.max_p=63};
	assert(nouveau_pll_calculate(&l,15000,&c)&&c.n==0&&c.actual_khz==15000);
	assert(nouveau_pll_plan(0,0x614100,0,&c,&p));
	zero_minima[9]=0;assert(!nouveau_pll_limits_v40(zero_minima,14,27000,&l));
	for(unsigned i=0;i<20000;i++) {
		uint8_t fuzz[14];for(unsigned j=0;j<14;j++)fuzz[j]=random32();
		if(nouveau_pll_limits_v40(fuzz,14,27000,&l)&&nouveau_pll_calculate(&l,1+random32()%1000000,&c)) {
			assert(c.m>=l.min_m&&c.m<=l.max_m&&c.n>=l.min_n&&c.n<=l.max_n&&c.p>=l.min_p&&c.p<=l.max_p);
			assert((uint64_t)l.input_min_khz*c.m<=l.ref_khz&&(uint64_t)l.input_max_khz*c.m>=l.ref_khz);
			assert(nouveau_pll_plan(0,0x614100,0,&c,&p));
		}
	}
	puts("NOUVEAU_PLL_PASS: v40 limits bounds, GT215 fractional golden coefficients, GF100 VPLL write plan, invalid clocks/registers, 20000 limits mutations");
}
