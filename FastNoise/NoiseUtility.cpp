#include <thread>
#include "headers\NoiseUtility.h"


//Must be called by the caller of noise producing functions
void CleanUpNoiseSIMD(float * resultArray)
{
	_aligned_free(resultArray);
}

//Must be called by the caller of noise producing functions
void CleanUpNoise(float * resultArray)
{
	free(resultArray);
}

void SphereSurfaceNoiseSIMDThread(int start, int end, int width, int height, Settings* S, float* xcos, float* ysin, ISIMDFractal3d fractalFunction,ISIMDNoise3d noiseFunction, SIMD* result, float *outMin, float *outMax)
{
	const float piOverHeight = pi / (height + 1);
	float phi = piOverHeight*start;
	float sinPhi;

	int count = start*width / VECTOR_SIZE;

	uSIMD min;
	uSIMD max;
	min.m = SetOne(999);
	max.m = SetOne(-999);
	for (int y = start; y < end; y = y + 1)
	{
		phi = phi + piOverHeight;
		S->z.m = SetOne(cosf(phi));
		sinPhi = sinf(phi);


		for (int x = 0; x < width - (VECTOR_SIZE - 1); x = x + VECTOR_SIZE)
		{

			for (int j = 0; j < VECTOR_SIZE; j++)
			{
				S->x.a[j] = xcos[x + j] * sinPhi;
				S->y.a[j] = ysin[x + j] * sinPhi;

			}

			fractalFunction(&result[count],S,noiseFunction);
			min.m = Min(min.m, result[count]);
			max.m = Max(max.m, result[count]);
			count = count + 1;
		}
	}

	*outMin = 999;
	*outMax = -999;
	for (int i = 0; i < VECTOR_SIZE; i++)
	{
		*outMin = fminf(*outMin, min.a[i]);
		*outMax = fmaxf(*outMax, max.a[i]);
	}

}

float* GetSphereSurfaceNoiseSIMD(int width, int height, int octaves, float lacunarity, float frequency, float gain, float offset, int fractalType, int noiseType, float* outMin, float *outMax)
{
	//SIMD data has to be aligned
	SIMD* result = (SIMD*)_aligned_malloc(width*height*  sizeof(float), MEMORY_ALIGNMENT);

	ISIMDFractal3d fractalFunction;
	ISIMDNoise3d noiseFunction;

	switch ((FractalType)fractalType)
	{
	case FBM: 
	{
		if (octaves == 1) fractalFunction = plainSIMD3d;
		else fractalFunction = fbmSIMD3d; 
		break;
	}
	case TURBULENCE:
	{
		if (octaves == 1) fractalFunction = plainSIMD3d;
		else fractalFunction = turbulenceSIMD3d; 
		break;
	}
	case RIDGE: 
	{
		if (octaves == 1) fractalFunction = ridgePlainSIMD3d;
		else fractalFunction = ridgeSIMD3d; 
		break;
	}
	case PLAIN: fractalFunction = plainSIMD3d; break;	
	{		
		fractalFunction = plainSIMD3d; break;
	}
	default:return 0;
	}


	switch ((NoiseType)noiseType)
	{
	case PERLIN:
		noiseFunction = perlinSIMD3d;
		break;
	case SIMPLEX:
	{
		initSIMDSimplex();
		noiseFunction = simplexSIMD3d;
	}
	}
	

	//set up spherical stuff
	int count = 0;
	const float twoPiOverWidth = twopi / width;
	float theta = 0;

	float* xcos = new float[width];
	float* ysin = new float[width];

	for (int x = 0; x < width; x = x + 1)
	{
		theta = theta + twoPiOverWidth;
		xcos[x] = cosf(theta);
		ysin[x] = sinf(theta);
	}

	unsigned cpuCount = 1; // std::thread::hardware_concurrency();

	std::thread* threads = new std::thread[cpuCount];
	int start = 0;
	float* min = new float[cpuCount];
	float* max = new float[cpuCount];
	Settings* S = new Settings[cpuCount];
	for (int i = 0; i < cpuCount; i++)
	{
		initSIMD(&S[i], frequency, lacunarity, offset, gain, octaves);
		int end = start + (height / cpuCount);
		//SphereSurfaceNoiseSIMDThread(int start, int end,int width, int height, Settings* S, SIMD* min, SIMD *max, float* xcos, float* ysin, ISIMDNoise fractalFunction, SIMD* result)
		threads[i] = std::thread(SphereSurfaceNoiseSIMDThread, start, end, width, height, &S[i], xcos, ysin, fractalFunction,noiseFunction, result, &min[i], &max[i]);
		start = end;
	}

	*outMin = 999;
	*outMax = -999;
	for (int i = 0; i < cpuCount; i++)
	{
		threads[i].join();
		*outMin = fminf(*outMin, min[i]);
		*outMax = fmaxf(*outMax, max[i]);
	}
	delete[] xcos;
	delete[] ysin;
	delete[] min;
	delete[] max;
	delete[] S;
	delete[] threads;
	return (float*)result;

}

float* GetSphereSurfaceNoise(int width, int height, int octaves, float lacunarity, float frequency, float gain, float offset, int fractalType, int noiseType, float *outMin, float *outMax)
{

	float* result = (float*)malloc(width*height*  sizeof(float));
	
	INoise3d noiseFunction;
	IFractal3d fractalFunction;

	switch ((NoiseType)fractalType)
	{
	case FBM: fractalFunction = fbm3d; break;
	case TURBULENCE: fractalFunction = turbulence3d; break;
	case RIDGE: fractalFunction = ridge3d; break;
	case PLAIN: fractalFunction = plain3d; break;

	default:return 0;
	}

	switch ((NoiseType)noiseType)
	{
	case PERLIN:
		noiseFunction = perlin3d;
		break;
	case SIMPLEX:
		noiseFunction = simplex3d;
	}



	//set up spherical stuff
	int count = 0;
	const float piOverHeight = pi / (height + 1);
	const float twoPiOverWidth = twopi / width;
	float phi = 0;
	float x3d, y3d, z3d;
	float sinPhi, theta;

	*outMin = 999;
	*outMax = -999;

	float* xcos = new float[width];
	float* ysin = new float[width];
	//Precalculate cos/sin	
	theta = 0;
	for (int x = 0; x < width; x = x + 1)
	{
		theta = theta + twoPiOverWidth;
		ysin[x] = sinf(theta);
		xcos[x] = cosf(theta);
	}

	for (int y = 0; y < height; y = y + 1)
	{
		phi = phi + piOverHeight;
		z3d = cosf(phi);
		sinPhi = sinf(phi);

		for (int x = 0; x < width; x = x + 1)
		{
			//use cos/sin lookup tables
			x3d = xcos[x] * sinPhi;
			y3d = ysin[x] * sinPhi;

			result[count] = fractalFunction(x3d, y3d, z3d, frequency, lacunarity, gain, octaves, offset,noiseFunction);

			*outMin = fminf(*outMin, result[count]);
			*outMax = fmaxf(*outMax, result[count]);
			count = count + 1;

		}
	}
	delete[] xcos;
	delete[] ysin;
	return result;

}
