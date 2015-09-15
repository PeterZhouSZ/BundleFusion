

#pragma once

#ifndef _IMAGE_MANAGER_H_
#define _IMAGE_MANAGER_H_

#include <windows.h>
#include <cuda_runtime.h>
#include <cutil_inline.h>

#include <vector>
#include <cassert>
#include <iostream>
#include <vector>

#include "GlobalDefines.h"
#include "cuda_SimpleMatrixUtil.h"

struct SIFTKeyPoint {
	float2 pos;
	float scale;
	float depth;
};

struct SIFTKeyPointDesc {
	unsigned char feature[128];
};

struct SIFTImageGPU {
	//int*					d_keyPointCounter;	//single counter value per image (into into global array)	//TODO we need this counter if we do multimatching
	SIFTKeyPoint*			d_keyPoints;		//array of key points (index into global array)
	SIFTKeyPointDesc*		d_keyPointDescs;	//array of key point descs (index into global array)
};

struct ImagePairMatch {
	int*		d_numMatches;		//single counter value per image
	float*		d_distances;		//array of distance (one per match)
	uint2*		d_keyPointIndices;	//array of index pair (one per match)	
};

//correspondence_idx -> image_Idx_i,j
struct EntryJ {
	unsigned int imgIdx_i;
	unsigned int imgIdx_j;
	float3 pos_i;
	float3 pos_j;

	__host__ __device__
	void setInvalid() {
		imgIdx_i = (unsigned int)-1;
		imgIdx_j = (unsigned int)-1;
	}
	__host__ __device__
	bool isValid() const {
		return imgIdx_i != (unsigned int)-1;
	}
};



class SIFTImageManager {
public:
	friend class SIFTMatchFilter;

	SIFTGPU_EXPORT SIFTImageManager(
		unsigned int maxImages = 500,
		unsigned int maxKeyPointsPerImage = 4096);

	SIFTGPU_EXPORT ~SIFTImageManager();


	SIFTGPU_EXPORT SIFTImageGPU& getImageGPU(unsigned int imageIdx);

	SIFTGPU_EXPORT const SIFTImageGPU& getImageGPU(unsigned int imageIdx) const;

	SIFTGPU_EXPORT unsigned int getNumImages() const;

	SIFTGPU_EXPORT unsigned int getNumKeyPointsPerImage(unsigned int imageIdx) const;

	SIFTGPU_EXPORT SIFTImageGPU& createSIFTImageGPU();

	SIFTGPU_EXPORT void finalizeSIFTImageGPU(unsigned int numKeyPoints);

	// ------- image-image matching (API for the Sift matcher)
	SIFTGPU_EXPORT ImagePairMatch& SIFTImageManager::getImagePairMatch(unsigned int prevImageIdx, uint2& keyPointOffset);

	//void resetImagePairMatches(unsigned int numImageMatches = (unsigned int)-1) {

	//	if (numImageMatches == (unsigned int)-1) numImageMatches = m_maxNumImages;
	//	assert(numImageMatches < m_maxNumImages);

	//	CUDA_SAFE_CALL(cudaMemset(d_currNumMatchesPerImagePair, 0, sizeof(int)*numImageMatches));
	//	CUDA_SAFE_CALL(cudaMemset(d_currMatchDistances, 0, sizeof(float)*numImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_RAW));
	//	CUDA_SAFE_CALL(cudaMemset(d_currMatchKeyPointIndices, -1, sizeof(uint2)*numImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_RAW));

	//	CUDA_SAFE_CALL(cudaMemset(d_currNumFilteredMatchesPerImagePair, 0, sizeof(int)*numImageMatches));
	//	CUDA_SAFE_CALL(cudaMemset(d_currFilteredMatchDistances, 0, sizeof(float)*numImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED));
	//	CUDA_SAFE_CALL(cudaMemset(d_currFilteredMatchKeyPointIndices, -1, sizeof(uint2)*numImageMatches*MAX_MATCHES_PER_IMAGE_PAIR_FILTERED));
	//}
	SIFTGPU_EXPORT void reset() {
		m_SIFTImagesGPU.clear();
		m_numKeyPointsPerImage.clear();
		m_numKeyPointsPerImagePrefixSum.clear();
		m_numKeyPoints = 0;
		m_globNumResiduals = 0;
		m_bFinalizedGPUImage = false;
		cutilSafeCall(cudaMemset(d_globNumResiduals, 0, sizeof(int)));

		// just to be safe
		std::vector<int> validImages(m_maxNumImages, 1);
		CUDA_SAFE_CALL(cudaMemcpy(validImages.data(), d_validImages, sizeof(int) * m_maxNumImages, cudaMemcpyDeviceToHost));
	}

	//sorts the key point matches inside image pair matches
	SIFTGPU_EXPORT void SortKeyPointMatchesCU(unsigned int numCurrImagePairs);

	SIFTGPU_EXPORT void FilterKeyPointMatchesCU(unsigned int numCurrImagePairs);

	SIFTGPU_EXPORT void AddCurrToResidualsCU(unsigned int numCurrImagePairs);

	SIFTGPU_EXPORT void InvalidateImageToImageCU(const uint2& imageToImageIdx);

	SIFTGPU_EXPORT void CheckForInvalidFramesCU(const int* d_variablesToCorrespondences, const int* d_varToCorrNumEntriesPerRow, unsigned int numVars, const uint2& imageIndices); // imageindices <- recently invalidated

	void getValidImagesDEBUG(std::vector<int>& valid) const {
		valid.resize(getNumImages());
		cutilSafeCall(cudaMemcpy(valid.data(), d_validImages, sizeof(int) * valid.size(), cudaMemcpyDeviceToHost));
	}
	void getSIFTKeyPointsDEBUG(std::vector<SIFTKeyPoint>& keys) const {
		keys.resize(m_numKeyPoints);
		cutilSafeCall(cudaMemcpy(keys.data(), d_keyPoints, sizeof(SIFTKeyPoint) * keys.size(), cudaMemcpyDeviceToHost));
	}
	void getSIFTKeyPointDescsDEBUG(std::vector<SIFTKeyPointDesc>& descs) const {
		descs.resize(m_numKeyPoints);
		cutilSafeCall(cudaMemcpy(descs.data(), d_keyPointDescs, sizeof(SIFTKeyPointDesc) * descs.size(), cudaMemcpyDeviceToHost));
	}
	void getRawKeyPointIndicesAndMatchDistancesDEBUG(unsigned int imagePairIndex, std::vector<uint2>& keyPointIndices, std::vector<float>& matchDistances) const
	{
		unsigned int numMatches;
		cutilSafeCall(cudaMemcpy(&numMatches, d_currNumMatchesPerImagePair + imagePairIndex, sizeof(unsigned int), cudaMemcpyDeviceToHost));
		keyPointIndices.resize(numMatches);
		matchDistances.resize(numMatches);
		cutilSafeCall(cudaMemcpy(keyPointIndices.data(), d_currMatchKeyPointIndices + imagePairIndex * MAX_MATCHES_PER_IMAGE_PAIR_RAW, sizeof(uint2) * numMatches, cudaMemcpyDeviceToHost));
		cutilSafeCall(cudaMemcpy(matchDistances.data(), d_currMatchDistances + imagePairIndex * MAX_MATCHES_PER_IMAGE_PAIR_RAW, sizeof(float) * numMatches, cudaMemcpyDeviceToHost));
	}
	void getFiltKeyPointIndicesAndMatchDistancesDEBUG(unsigned int imagePairIndex, std::vector<uint2>& keyPointIndices, std::vector<float>& matchDistances) const
	{
		unsigned int numMatches;
		cutilSafeCall(cudaMemcpy(&numMatches, d_currNumFilteredMatchesPerImagePair + imagePairIndex, sizeof(unsigned int), cudaMemcpyDeviceToHost));
		keyPointIndices.resize(numMatches);
		matchDistances.resize(numMatches);
		cutilSafeCall(cudaMemcpy(keyPointIndices.data(), d_currFilteredMatchKeyPointIndices + imagePairIndex * MAX_MATCHES_PER_IMAGE_PAIR_FILTERED, sizeof(uint2) * numMatches, cudaMemcpyDeviceToHost));
		cutilSafeCall(cudaMemcpy(matchDistances.data(), d_currFilteredMatchKeyPointIndices + imagePairIndex * MAX_MATCHES_PER_IMAGE_PAIR_FILTERED, sizeof(float) * numMatches, cudaMemcpyDeviceToHost));
	}
	void getFiltKeyPointIndicesDEBUG(unsigned int imagePairIndex, std::vector<uint2>& keyPointIndices) const
	{
		unsigned int numMatches;
		cutilSafeCall(cudaMemcpy(&numMatches, d_currNumFilteredMatchesPerImagePair + imagePairIndex, sizeof(unsigned int), cudaMemcpyDeviceToHost));
		keyPointIndices.resize(numMatches);
		cutilSafeCall(cudaMemcpy(keyPointIndices.data(), d_currFilteredMatchKeyPointIndices + imagePairIndex * MAX_MATCHES_PER_IMAGE_PAIR_FILTERED, sizeof(uint2) * numMatches, cudaMemcpyDeviceToHost));
	}
	EntryJ* getGlobalCorrespondencesDEBUG() { return d_globMatches; }
	unsigned int getNumGlobalCorrespondences() const { return m_globNumResiduals; }


	//!!!TODO where to put transforms
	SIFTGPU_EXPORT void fuseToGlobal(SIFTImageManager* global, const float4x4* transforms, unsigned int numTransforms) const;

	SIFTGPU_EXPORT static void TestSVDDebugCU(const float3x3& m);

	SIFTGPU_EXPORT void saveToFile(const std::string& s);

	SIFTGPU_EXPORT void loadFromFile(const std::string& s);
private:

	void alloc();
	void free();
	void initializeMatching();

	// keypoints & descriptors
	std::vector<SIFTImageGPU>	m_SIFTImagesGPU;			// TODO if we ever do a global multi-match kernel, then we need this array on the GPU
	bool						m_bFinalizedGPUImage;

	unsigned int				m_numKeyPoints;						//current fill status of key point counts
	std::vector<unsigned int>	m_numKeyPointsPerImage;				//synchronized with key point counters;
	std::vector<unsigned int>	m_numKeyPointsPerImagePrefixSum;	//prefix sum of the array above

	SIFTKeyPoint*			d_keyPoints;		//array of all key points ever found	(linearly stored)
	SIFTKeyPointDesc*		d_keyPointDescs;	//array of all descriptors every found	(linearly stored)
	//int*					d_keyPointCounters;	//atomic counter once per image			(GPU array of int-valued counters)	// TODO incase we do a multi-match kernel we need this


	// matching
	std::vector<ImagePairMatch>	m_currImagePairMatches;		//image pair matches of the current frame

	int*			d_currNumMatchesPerImagePair;	// #key point matches
	float*			d_currMatchDistances;			// array of distances per key point pair
	uint2*			d_currMatchKeyPointIndices;		// array of indices to d_keyPoints
	
	int*			d_currNumFilteredMatchesPerImagePair;	// #key point matches
	float*			d_currFilteredMatchDistances;			// array of distances per key point pair
	uint2*			d_currFilteredMatchKeyPointIndices;		// array of indices to d_keyPoints
	float4x4*		d_currFilteredTransforms;				// array of transforms estimated in the first filter stage

	int*			d_validImages;

	unsigned int	m_globNumResiduals;
	int*			d_globNumResiduals;
	EntryJ*			d_globMatches;
	uint2*			d_globMatchesKeyPointIndices;


	unsigned int m_maxNumImages;			//max number of images maintained by the manager
	unsigned int m_maxKeyPointsPerImage;	//max number of SIFT key point that can be detected per image
};


#endif
