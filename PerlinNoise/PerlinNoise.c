#include <sys\timeb.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <xmmintrin.h> //SSE
#include <emmintrin.h> //SSE 2
#include <smmintrin.h> // SSE4.1
#include <immintrin.h> //avx

// Original Author: Stefan Gustavson (stegu@itn.liu.se)
//
// This library is public domain software, released by the author
// into the public domain in February 2011. You may do anything
// you like with it. You may even remove all attributions,
// but of course I'd appreciate it if you kept my name somewhere.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.

/** \file
\brief Implements the Noise1234 class for producing Perlin noise.
\author Stefan Gustavson (stegu@itn.liu.se)
*/

/*
* This implementation is "Improved Noise" as presented by
* Ken Perlin at Siggraph 2002. The 3D function is a direct port
* of his Java reference code available on www.noisemachine.com
* (although I cleaned it up and made the code more readable),
* but the 1D, 2D and 4D cases were implemented from scratch
* by me.
*
* This is a highly reusable class. It has no dependencies
* on any other file, apart from its own header file.
*/


/* Jack Mott - SIMD conversion, addition of fbm, turbulence, and ridge variations
*/


// For non SIMD only
#define FADE(t) ( t * t * t * ( t * ( t * 6 - 15 ) + 10 ) )
#define FASTFLOOR(x) ( ((x)>0) ? ((int)x) : ((int)x-1 ) )
#define LERP(t, a, b) ((a) + (t)*((b)-(a)))

#define SSE2  //indicates we want SSE2
#define SSE42 //indicates we want SSE4.2 instructions (floor is available)
#define AVX2 //indicates we want AVX2 instructions (gather is available)


//creat types we can use in either the 128 or 256 case
#ifndef AVX2
typedef __m128 SIMD;
typedef __m128i SIMDi;
#define VECTOR_SIZE 4
#define SetOne(x) _mmSetOne_ps(x)

#endif
#ifdef AVX2
typedef __m256 SIMD;
typedef __m256i SIMDi;
#define VECTOR_SIZE 8
#define Store(x,y) _mm256_store_ps(x,y)
#define SetOne(x) _mm256_set1_ps(x)
#define SetZero() _mm256_setzero_ps()
#define SetOnei(x) _mm256_set1_epi32(x)
#define SetZeroi(x) _mm256_setzero_epi32(x)
#define Add(x,y) _mm256_add_ps(x,y)
#define Sub(x,y) _mm256_sub_ps(x,y)
#define Addi(x,y) _mm256_add_epi32(x,y)
#define Subi(x,y) _mm256_sub_epi32(x,y)
#define Mul(x,y) _mm256_mul_ps(x,y)
#define Muli(x,y) _mm256_mul_epi32(x,y)
#define And(x,y) _mm256_and_ps(x,y)
#define Andi(x,y) _mm256_and_si256(x,y)
#define AndNot(x,y) _mm256_andnot_ps(x,y)
#define AndNoti(x,y) _mm256_andnot_epi32(x,y)
#define Or(x,y) _mm256_or_ps(x,y)
#define Ori(x,y) _mm256_or_si256(x,y)
#define CastToFloat(x) _mm256_castsi256_ps(x)
#define CastToInt(x) _mm256_castps_si256(x)
#define ConvertToInt(x) _mm256_cvtps_epi32(x)
#define ConvertToFloat(x) _mm256_cvtepi32_ps(x)
#define Equal(x,y)  _mm256_cmp_ps(x,y,_CMP_EQ_OQ) 
#define Equali(x,y) _mm256_cmpeq_epi32(x,y)
#define GreaterThan(x,y) _mm256_cmp_ps(x,y,_CMP_GT_OQ)
#define GreaterThani(x,y) _mm256_cmpgt_epi32(x,y)
#define LessThan(x,y) _mm256_cmp_ps(x,y,_CMP_LT_OQ)
#define LessThani(x,y) _mm256_cmpgt_epi32(y,x) 
#define NotEqual(x,y) _mm256_cmp_ps(x,y,_CMP_NEQ_OQ)
#define Floor(x) _mm256_floor_ps(x)
#define Max(x,y) _mm256_max_ps(x,y)
#define Maxi(x,y) _mm256_max_epi32(x,y)

#endif

//We use this union hack for easy
//access to the floats for unvectorizeable
//lookup table access
union uSIMDi {
	SIMDi m;
	int a[VECTOR_SIZE];
};

union uSIMD {
	SIMD m;
	float a[VECTOR_SIZE];
};


//constants
SIMDi one, two, four, eight, twelve, fourteen, fifteeni, ff;
SIMD minusonef, zero, onef, six, fifteen, ten, scale;


const float pi = 3.14159265359f;
const float twopi = 6.2831853f;

typedef SIMD (*ISIMDNoise)(SIMD*, SIMD*, SIMD*, SIMD*, SIMD*,SIMD*, SIMD*, int);
typedef float(*INoise)(float, float, float, float, float,float,float, int);

void init(int octaves)
{

	//integer constants	
	one = SetOnei(1);
	two = SetOnei(2);
	four = SetOnei(4);
	eight = SetOnei(8);
	twelve = SetOnei(12);
	fourteen = SetOnei(14);
	fifteeni = SetOnei(15);
	ff = SetOnei(0xff);

	//float constants
	minusonef = SetOne(-1);
	zero = SetZero();
	onef = SetOne(1);
	six = SetOne(6);
	ten = SetOne(10);
	fifteen = SetOne(15);

	//final scaling constant
	scale = SetOne(.936f);

}


//To scale output to [0..1]
inline void SetUpOffsetScale(int octaves, SIMD* fbmOffset, SIMD* fbmScale)
{
	switch (octaves)
	{
	case 1:
		*fbmOffset = SetZero();
		*fbmScale = SetOne(1.066f);
		break;
	case 2:
		*fbmOffset = SetOne(.073f);
		*fbmScale = SetOne(.8584f);
		break;
	case 3:
		*fbmOffset = SetOne(.1189f);
		*fbmScale = SetOne(.8120f);
		break;
	case 4:
		*fbmOffset = SetOne(.1440f);
		*fbmScale = SetOne(.8083f);
		break;
	case 5:
		*fbmOffset = SetOne(.1530f);
		*fbmScale = SetOne(.8049f);
		break;
	default:
		*fbmOffset = SetOne(.16f);
		*fbmScale = SetOne(.8003f);
	}

}

//---------------------------------------------------------------------
// Static data

/*
* Permutation table. This is just a random jumble of all numbers 0-255,
* repeated twice to avoid wrapping the index at 255 for each lookup.
* This needs to be exactly the same for all instances on all platforms,
* so it's easiest to just keep it as static explicit data.
* This also removes the need for any initialisation of this class.
*
* Note that making this an int[] instead of a char[] might make the
* code run faster on platforms with a high penalty for unaligned single
* byte addressing. Intel x86 is generally single-byte-friendly, but
* some other CPUs are faster with 4-aligned reads.
* However, a char[] is smaller, which avoids cache trashing, and that
* is probably the most important aspect on most architectures.
* This array is accessed a *lot* by the noise functions.
* A vector-valued noise over 3D accesses it 96 times, and a
* float-valued 4D noise 64 times. We want this to fit in the cache!
*/
#ifndef AVX2
const unsigned char perm[] = 
#endif
#ifdef AVX2
const uint32_t perm[] =
#endif
{ 151,160,137,91,90,15,
131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,21,10,23,
190, 6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,
88,237,149,56,87,174,20,125,136,171,168, 68,175,74,165,71,134,139,48,27,166,
77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,55,46,245,40,244,
102,143,54, 65,25,63,161, 1,216,80,73,209,76,132,187,208, 89,18,169,200,196,
135,130,116,188,159,86,164,100,109,198,173,186, 3,64,52,217,226,250,124,123,
5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,
223,183,170,213,119,248,152, 2,44,154,163, 70,221,153,101,155,167, 43,172,9,
129,22,39,253, 19,98,108,110,79,113,224,232,178,185, 112,104,218,246,97,228,
251,34,242,193,238,210,144,12,191,179,162,241, 81,51,145,235,249,14,239,107,
49,192,214, 31,181,199,106,157,184, 84,204,176,115,121,50,45,127, 4,150,254,
138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180,
151,160,137,91,90,15,
131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,21,10,23,
190, 6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,
88,237,149,56,87,174,20,125,136,171,168, 68,175,74,165,71,134,139,48,27,166,
77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,55,46,245,40,244,
102,143,54, 65,25,63,161, 1,216,80,73,209,76,132,187,208, 89,18,169,200,196,
135,130,116,188,159,86,164,100,109,198,173,186, 3,64,52,217,226,250,124,123,
5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,
223,183,170,213,119,248,152, 2,44,154,163, 70,221,153,101,155,167, 43,172,9,
129,22,39,253, 19,98,108,110,79,113,224,232,178,185, 112,104,218,246,97,228,
251,34,242,193,238,210,144,12,191,179,162,241, 81,51,145,235,249,14,239,107,
49,192,214, 31,181,199,106,157,184, 84,204,176,115,121,50,45,127, 4,150,254,
138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
};

//---------------------------------------------------------------------

/*
* Helper functions to compute gradients-dot-residualvectors (1D to 4D)
* Note that these generate gradients of more than unit length. To make
* a close match with the value range of classic Perlin noise, the final
* noise values need to be rescaled. To match the RenderMan noise in a
* statistical sense, the approximate scaling values (empirically
* determined from test renderings) are:
* 1D noise needs rescaling with 0.188
* 2D noise needs rescaling with 0.507
* 3D noise needs rescaling with 0.936
* 4D noise needs rescaling with 0.87
* Note that these noise functions are the most practical and useful
* signed version of Perlin noise. To return values according to the
* RenderMan specification from the SL noise() and pnoise() functions,
* the noise values need to be scaled and offset to [0,1], like this:
* float SLnoise = (Noise1234::noise(x,y,z) + 1.0) * 0.5;
*/

inline float grad(int hash, float x, float y, float z) {
	int h = hash & 15;     // Convert low 4 bits of hash code into 12 simple
	float u = h < 8 ? x : y; // gradient directions, and compute dot product.
	float v = h < 4 ? y : h == 12 || h == 14 ? x : z; // Fix repeats at h = 12 to 15
	return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}


inline SIMD  gradV(SIMDi *hash, SIMD *x, SIMD *y, SIMD *z) {

	SIMDi h = Andi(*hash, fifteeni);
	SIMD h1 = NotEqual(zero, CastToFloat(Andi(h, one)));
	SIMD h2 = NotEqual(zero, CastToFloat(Andi(h, two)));


	//if h < 8 then x, else y
	SIMD u = CastToFloat(LessThani(h, eight));
	u = Or(And(u, *x), AndNot(u, *y));

	//if h < 4 then y else if h is 12 or 14 then x else z
	SIMD v = CastToFloat(LessThani(h, four));
	SIMD h12o14 = NotEqual(zero, CastToFloat(Ori(Equali(h, twelve), Equali(h, fourteen))));
	h12o14 = Or(And(h12o14, *x), AndNot(h12o14, *z));
	v = Or(And(v, *y), AndNot(v, h12o14));

	//get -u and -v
	SIMD minusU = Sub(zero,u);
	SIMD minusV = Sub(zero,v);

	//if h1 then -u else u
	u = Or(And(h1, minusU), AndNot(h1, u));
	//if h2 then -v else v
	v = Or(And(h2, minusV), AndNot(h2, v));
	return Add(u, v);
}


inline SIMD noiseSIMD(SIMD* x, SIMD* y, SIMD* z)
{
	union uSIMDi ix0, iy0, ix1, iy1, iz0, iz1;
	SIMD fx0, fy0, fz0, fx1, fy1, fz1;

	//mm_floor is the only SSE4 instruction
	//you can get SSE2 compatbility by rolling your own floor
	ix0.m = ConvertToInt(Floor(*x));
	iy0.m = ConvertToInt(Floor(*y));
	iz0.m = ConvertToInt(Floor(*z));


	fx0 = Sub(*x, ConvertToFloat(ix0.m));
	fy0 = Sub(*y, ConvertToFloat(iy0.m));
	fz0 = Sub(*z, ConvertToFloat(iz0.m));

	fx1 = Sub(fx0, onef);
	fy1 = Sub(fy0, onef);
	fz1 = Sub(fz0, onef);

	ix1.m = Andi(Addi(ix0.m, one), ff);
	iy1.m = Andi(Addi(iy0.m, one), ff);
	iz1.m = Andi(Addi(iz0.m, one), ff);

	ix0.m = Andi(ix0.m, ff);
	iy0.m = Andi(iy0.m, ff);
	iz0.m = Andi(iz0.m, ff);


	SIMD
		r = Mul(fz0, six);
	r = Sub(r, fifteen);
	r = Mul(r, fz0);
	r = Add(r, ten);
	r = Mul(r, fz0);
	r = Mul(r, fz0);
	r = Mul(r, fz0);

	SIMD
		t = Mul(fy0, six);
	t = Sub(t, fifteen);
	t = Mul(t, fy0);
	t = Add(t, ten);
	t = Mul(t, fy0);
	t = Mul(t, fy0);
	t = Mul(t, fy0);

	SIMD
		s = Mul(fx0, six);
	s = Sub(s, fifteen);
	s = Mul(s, fx0);
	s = Add(s, ten);
	s = Mul(s, fx0);
	s = Mul(s, fx0);
	s = Mul(s, fx0);

	union uSIMDi p1, p2, p3, p4, p5, p6, p7, p8;
#ifndef AVXff
	
	for (int i = 0; i < VECTOR_SIZE; i++)
	{
		p1.a[i] = perm[ix0.a[i] + perm[iy0.a[i] + perm[iz0.a[i]]]];
		p2.a[i] = perm[ix0.a[i] + perm[iy0.a[i] + perm[iz1.a[i]]]];
		p3.a[i] = perm[ix0.a[i] + perm[iy1.a[i] + perm[iz0.a[i]]]];
		p4.a[i] = perm[ix0.a[i] + perm[iy1.a[i] + perm[iz1.a[i]]]];
		p5.a[i] = perm[ix1.a[i] + perm[iy0.a[i] + perm[iz0.a[i]]]];
		p6.a[i] = perm[ix1.a[i] + perm[iy0.a[i] + perm[iz1.a[i]]]];
		p7.a[i] = perm[ix1.a[i] + perm[iy1.a[i] + perm[iz0.a[i]]]];	
		p8.a[i] = perm[ix1.a[i] + perm[iy1.a[i] + perm[iz1.a[i]]]];

	}
#endif // !AVX
#ifdef AVX2ff
	SIMDi pz0, pz1, pz0y0, pz0y1, pz1y1, pz1y0;

	pz0 = _mm_i32gather_epi32(perm, iz0.m, 4);
	pz1 = _mm_i32gather_epi32(perm, iz1.m, 4);
	
	pz0y0 = _mm_i32gather_epi32(perm, Addi(iy0.m, pz0), 4);
	pz0y1 = _mm_i32gather_epi32(perm, Addi(iy1.m, pz0), 4);
	pz1y0 = _mm_i32gather_epi32(perm, Addi(iy0.m, pz1), 4);
	pz1y1 = _mm_i32gather_epi32(perm, Addi(iy1.m, pz1), 4);

	p1.m = Addi(ix0.m,pz0y0);
	p1.m = _mm_i32gather_epi32(perm, p1.m, 4);

	p2.m = Addi(ix0.m, pz1y0);
	p2.m = _mm_i32gather_epi32(perm, p2.m, 4);

	p3.m = Addi(ix0.m,pz0y1);
	p3.m = _mm_i32gather_epi32(perm, p3.m, 4);

	p4.m = Addi(ix0.m, pz1y1);
	p4.m = _mm_i32gather_epi32(perm, p4.m, 4);

	p5.m = Addi(ix1.m, pz0y0);
	p5.m = _mm_i32gather_epi32(perm, p5.m, 4);

	p6.m = Addi(ix1.m, pz1y0);
	p6.m = _mm_i32gather_epi32(perm, p6.m, 4);

	p7.m = Addi(ix1.m, pz0y1);
	p7.m = _mm_i32gather_epi32(perm, p7.m, 4);

	p8.m = Addi(ix1.m, pz1y1);
	p8.m = _mm_i32gather_epi32(perm, p8.m, 4);


#endif // AVX

	//This section may be vectorizeable with AVX gather instructions.	
	

	SIMD nxy0 = gradV(&p1.m, &fx0, &fy0, &fz0);
	SIMD nxy1 = gradV(&p2.m, &fx0, &fy0, &fz1);
	SIMD nx0 = Add(nxy0, Mul(r, Sub(nxy1, nxy0)));

	nxy0 = gradV(&p3.m, &fx0, &fy1, &fz0);
	nxy1 = gradV(&p4.m, &fx0, &fy1, &fz1);
	SIMD nx1 = Add(nxy0, Mul(r, Sub(nxy1, nxy0)));

	SIMD n0 = Add(nx0, Mul(t, Sub(nx1, nx0)));

	nxy0 = gradV(&p5.m, &fx1, &fy0, &fz0);
	nxy1 = gradV(&p6.m, &fx1, &fy0, &fz1);
	nx0 = Add(nxy0, Mul(r, Sub(nxy1, nxy0)));

	nxy0 = gradV(&p7.m, &fx1, &fy1, &fz0);
	nxy1 = gradV(&p8.m, &fx1, &fy1, &fz1);
	nx1 = Add(nxy0, Mul(r, Sub(nxy1, nxy0)));

	SIMD n1 = Add(nx0, Mul(t, Sub(nx1, nx0)));

	n1 = Add(n0, Mul(s, Sub(n1, n0)));
	return Mul(scale, n1);



}



//---------------------------------------------------------------------
/** 3D float Perlin noise.
*/
float noise(float x, float y, float z)
{
	int ix0, iy0, ix1, iy1, iz0, iz1;
	float fx0, fy0, fz0, fx1, fy1, fz1;
	float s, t, r;
	float nxy0, nxy1, nx0, nx1, n0, n1;

	ix0 = FASTFLOOR(x); // (int)x; // Integer part of x
	iy0 = FASTFLOOR(y); // Integer part of y
	iz0 = FASTFLOOR(z); // Integer part of z
	fx0 = x - ix0;        // Fractional part of x
	fy0 = y - iy0;        // Fractional part of y
	fz0 = z - iz0;        // Fractional part of z
	fx1 = fx0 - 1.0f;
	fy1 = fy0 - 1.0f;
	fz1 = fz0 - 1.0f;
	ix1 = (ix0 + 1) & 0xff; // Wrap to 0..255
	iy1 = (iy0 + 1) & 0xff;
	iz1 = (iz0 + 1) & 0xff;
	ix0 = ix0 & 0xff;
	iy0 = iy0 & 0xff;
	iz0 = iz0 & 0xff;

	r = FADE(fz0);
	t = FADE(fy0);
	s = FADE(fx0);

	nxy0 = grad(perm[ix0 + perm[iy0 + perm[iz0]]], fx0, fy0, fz0);
	nxy1 = grad(perm[ix0 + perm[iy0 + perm[iz1]]], fx0, fy0, fz1);
	nx0 = LERP(r, nxy0, nxy1);

	nxy0 = grad(perm[ix0 + perm[iy1 + perm[iz0]]], fx0, fy1, fz0);
	nxy1 = grad(perm[ix0 + perm[iy1 + perm[iz1]]], fx0, fy1, fz1);
	nx1 = LERP(r, nxy0, nxy1);

	n0 = LERP(t, nx0, nx1);

	nxy0 = grad(perm[ix1 + perm[iy0 + perm[iz0]]], fx1, fy0, fz0);
	nxy1 = grad(perm[ix1 + perm[iy0 + perm[iz1]]], fx1, fy0, fz1);
	nx0 = LERP(r, nxy0, nxy1);

	nxy0 = grad(perm[ix1 + perm[iy1 + perm[iz0]]], fx1, fy1, fz0);
	nxy1 = grad(perm[ix1 + perm[iy1 + perm[iz1]]], fx1, fy1, fz1);
	nx1 = LERP(r, nxy0, nxy1);

	n1 = LERP(t, nx0, nx1);

	return 0.936f * (LERP(s, n0, n1));
}

//Fractal brownian motions using SIMD
inline SIMD fbmSIMD(SIMD *vX, SIMD *vY, SIMD *vZ, SIMD* vLacunarity, SIMD* vGain, SIMD* vFrequency, SIMD* vOffset, int octaves)
{
	SIMD vSum, vAmplitude;
	vSum = SetZero();	
	vAmplitude = SetOne(0.5f);
	for (int i = 0; i < octaves; i++)
	{
		SIMD vfx = Mul(*vX,*vFrequency);
		SIMD vfy = Mul(*vY,*vFrequency);
		SIMD vfz = Mul(*vZ,*vFrequency);		
		vSum = Add(vSum, Mul(vAmplitude, noiseSIMD(&vfx, &vfy, &vfz)));
		*vFrequency = Mul(*vFrequency, *vLacunarity);
		vAmplitude = Mul(vAmplitude, *vGain);
	}


	return vSum;

}

//fractal brownian motion without SIMD
inline float fbm(float x, float y, float z, float lacunarity, float gain, float frequency, float offset, int octaves)
{
	float sum = 0;
	float amplitude = 1;	
	for (int i = 0; i < octaves; i++)
	{
		sum += noise(x, y, z)*frequency;
		frequency *= lacunarity;
		amplitude *= gain;
	}
	return sum;
}


//turbulence  using SIMD
inline SIMD turbulenceSIMD(SIMD *vX, SIMD *vY, SIMD *vZ, SIMD* vLacunarity, SIMD* vGain, SIMD* vFrequency, SIMD* vOffset, int octaves)
{
	SIMD vSum, vAmplitude;
	vSum = SetZero();
	vAmplitude = SetOne(1.0f);
	for (int i = 0; i < octaves; i++)
	{
		SIMD vfx = Mul(*vX, *vFrequency);
		SIMD vfy = Mul(*vY, *vFrequency);
		SIMD vfz = Mul(*vZ, *vFrequency);
		SIMD r = Mul(vAmplitude, noiseSIMD(&vfx, &vfy, &vfz));
		//get abs of r by trickery
		r = Max(Sub(zero, r), r);
		vSum = Add( vSum,r);
		*vFrequency = Mul(*vFrequency, *vLacunarity);
		vAmplitude = Mul(vAmplitude, *vGain);
	}


	return vSum;

}

inline float turbulence(float x, float y, float z, float lacunarity, float gain, float frequency, float offset, int octaves)
{
	float sum = 0;	
	float amplitude = 1;
	for (int i = 0; i < octaves; i++)
	{
		sum += (float)fabs(noise(x*frequency, y*frequency, z*frequency)*amplitude);
		frequency *= lacunarity;
		amplitude *= gain;
	}
	return sum;
}


inline SIMD ridgeSIMD(SIMD *vX, SIMD *vY, SIMD *vZ, SIMD* vLacunarity, SIMD* vGain, SIMD* vFrequency, SIMD* vOffset, int octaves)
{
	SIMD vSum, vAmplitude, vPrev;
	vSum = SetZero();
	vAmplitude = SetOne(1.0f);
	vPrev = SetOne(1.0f);
	for (int i = 0; i < octaves; i++)
	{
		SIMD vfx = Mul(*vX, *vFrequency);
		SIMD vfy = Mul(*vY, *vFrequency);
		SIMD vfz = Mul(*vZ, *vFrequency);
		SIMD r =  noiseSIMD(&vfx, &vfy, &vfz);
		//get abs of r by trickery
		r = Max(Sub(zero, r), r);
		r = Sub(*vOffset, r);
		r = Mul(r, r);
		r = Mul(r, vAmplitude);
		r = Mul(r, vPrev);
		vSum = Add(vSum, r);
		*vFrequency = Mul(*vFrequency, *vLacunarity);
		vAmplitude = Mul(vAmplitude, *vGain);
	}


	return vSum;

}

inline float ridge(float x, float y, float z, float lacunarity, float gain, float frequency, float offset, int octaves)
{
	float sum = 0;
	float amplitude = 0.5f;
	float prev = 1.0f;
	for (int i = 0; i < octaves; i++)
	{
		float r = (float)fabs(noise(x*frequency, y*frequency, z*frequency));
		r= offset - r;
		r = r*r;
		sum += r*amplitude*prev;
		frequency *= lacunarity;
		amplitude *= gain;
	}
	return sum;
}

//---------------------------------------------------------------------
//Get noise on the surface of a sphere
//This could be SIMD up as wel, but was not clear to me what cpus 
//support SVML for the transcendental math. Not going to speed things up
//much either.

void CleanUpNoise(float * resultArray)
{
	_aligned_free(resultArray);
}

float* GetSphericalNoiseSIMD(int width, int height, int octaves, float lacunarity, float gain, float stretch, float offsetx, float offsety, ISIMDNoise noise)
{
	init(octaves);

	float *result = (float*)_aligned_malloc(width*height*  sizeof(float), 16);

	//Debugging shows this keeps them closer together in memory
	float *VxStore = (float*)_aligned_malloc(3 * 4 * sizeof(float), 16);
	float *VyStore = VxStore + 4;
	float *VzStore = VxStore + 8;


	//set up spherical stuff
	int count = 0;
	const float piOverHeight = pi / height;
	const float twoPiOverWidth = twopi / width;
	float phi = 0;
	float x3d, y3d, z3d;
	float sinPhi, theta;

	SIMD fbmOffset, fbmScale;
	SetUpOffsetScale(octaves, &fbmOffset, &fbmScale);

	SIMD vGain = SetOne(gain);
	SIMD vLacunarity = SetOne(lacunarity);
	SIMD vFrequency = SetOne(1);
	SIMD vOffset = SetOne(1);

	for (int y = 0; y < height; y = y + 1)
	{
		phi = phi + piOverHeight;
		z3d = cosf(phi)*stretch;
		sinPhi = sinf(phi);
		theta = 0;
		for (int x = 0; x < width - (VECTOR_SIZE-1); x = x + VECTOR_SIZE)
		{

			for (int j = 0; j < VECTOR_SIZE; j++)
			{
				theta = theta + twoPiOverWidth;
				x3d = cosf(theta) * sinPhi;
				y3d = sinf(theta) * sinPhi;

				VxStore[j] = x3d * 2 + offsetx;
				VyStore[j] = y3d * 2 + offsety;
				VzStore[j] = z3d * 2;

			}
			SIMD r = noise((SIMD*)VxStore, (SIMD*)VyStore, (SIMD*)VzStore, &vGain, &vLacunarity,&vFrequency,&vOffset, octaves);
			Store(&result[count], Mul(Add(r, fbmOffset), fbmScale));
			count = count + 4;

		}
	}
	_aligned_free(VxStore);
	return result;

}


#define TEST_COUNT 4096
void testNoise(INoise n)
{
	struct timeb starttime, endtime;
	int diff;
	ftime(&starttime);

	float accumulator = 0;
	for (int x = 0; x < TEST_COUNT; x++)
	{
		for (int y = 0; y < TEST_COUNT; y++)
		{
			accumulator += n(x, y, x, 1.0f, 1.0f, 1.0f,1.0f,1);
		}
	}

	printf("%f\n", accumulator);
	ftime(&endtime);
	diff = (int)(1000.0 * (endtime.time - starttime.time)
		+ (endtime.millitm - starttime.millitm));

	printf("Time Taken for Non SIMD: %d\n", diff);
	
	
}

void testSIMDNoise(ISIMDNoise n)
{
	struct timeb starttime, endtime;
	int diff;
	ftime(&starttime);



	//Debugging shows this keeps them closer together in memory
	float *VxStore = (float*)_aligned_malloc(3 * VECTOR_SIZE * sizeof(float), 16);
	float *VyStore = VxStore + VECTOR_SIZE;
	float *VzStore = VxStore + VECTOR_SIZE*2;
	SIMD result = SetZero();
	SIMD vGain = SetOne(1);
	SIMD vLacunarity = SetOne(1);
	SIMD vFrequency = SetOne(1);
	SIMD vOffset = SetOne(1);
	int octaves = 1;
	for (int x = 0; x < TEST_COUNT; x++)
	{
		for (int y = 0; y < TEST_COUNT - (VECTOR_SIZE-1); y = y + VECTOR_SIZE)
		{
			for (int j = 0; j < VECTOR_SIZE; j++)
			{

				VxStore[j] = (float)x;
				VyStore[j] = (float)y;
				VzStore[j] = (float)x;
			}

			result = Add(result, n((SIMD*)VxStore, (SIMD*)VyStore, (SIMD*)VzStore, &vGain, &vLacunarity,&vFrequency,&vOffset, octaves));
		}
	}



	ftime(&endtime);
	diff = (int)(1000.0 * (endtime.time - starttime.time)
		+ (endtime.millitm - starttime.millitm));

	printf("Time Taken for SIMD: %d\n", diff);
	
}

int main()
{
	//to evaluate how fast we went			
	//float *x = GetSphericalPerlinNoise(4096, 4096, 3, 2, 0.5, 1, 0, 0,fbmSIMD);
	testNoise(ridge);
	testSIMDNoise(ridgeSIMD);
	int xy;
	scanf("%d", &xy);


}


