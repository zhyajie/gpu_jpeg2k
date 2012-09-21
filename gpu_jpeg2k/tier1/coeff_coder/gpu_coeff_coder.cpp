/**
 * @file gpu_coeff_coder.cpp
 *
 * @brief Coefficients coder.
 */

extern "C" {
	#include "gpu_coder.h"
	#include "../../misc/memory_management.cuh"
	#include "../../print_info/print_info.h"
}

#include <iostream>
#include <string>
#include <fstream>

//#include <libxml++/libxml++.h>

#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#include <list>

#include <cuda_runtime.h>
#include <cuda_runtime_api.h>

#include "gpu_coeff_coder2.cuh"
#include "coeff_coder_pcrd.cuh"

//TODO: shouldn't those two methodes be moved to some more generic place, so that they can  be used by all CUDA calls?
#define CHECK_ERRORS_WITH_SYNC(stmt) \
		stmt; \
		{ \
		cudaThreadSynchronize(); \
\
		cudaError_t error; \
		if(error = cudaGetLastError()) \
			std::cout << "Error in " << __FILE__ << " at " << __LINE__ << " line: " << cudaGetErrorString(error) << std::endl; \
		};

#define CHECK_ERRORS_WITHOUT_SYNC(stmt) \
		stmt; \
		{ \
\
		cudaError_t error; \
		if(error = cudaGetLastError()) \
			std::cout << "Error in " << __FILE__ << " at " << __LINE__ << " line: " << cudaGetErrorString(error) << std::endl; \
		};

#define CHECK_ERRORS(stmt) CHECK_ERRORS_WITH_SYNC(stmt)

float gpuEncode(EntropyCodingTaskInfo *infos, type_image *img, int count, int targetSize)
{
	int codeBlocks = count;
	int maxOutLength = MAX_CODESTREAM_SIZE;

//	long int start_bebcot = start_measure();
	int n = 0;
	for(int i = 0; i < codeBlocks; i++)
		n += infos[i].width * infos[i].height;

	mem_mg_t *mem_mg = img->mem_mg;
	CodeBlockAdditionalInfo *h_infos = (CodeBlockAdditionalInfo *)mem_mg->alloc->host(sizeof(CodeBlockAdditionalInfo) * codeBlocks, mem_mg->ctx);
	CodeBlockAdditionalInfo *d_infos = (CodeBlockAdditionalInfo *)mem_mg->alloc->dev(sizeof(CodeBlockAdditionalInfo) * codeBlocks, mem_mg->ctx);
	byte *d_outbuf = (byte *)mem_mg->alloc->dev(sizeof(byte) * codeBlocks * maxOutLength, mem_mg->ctx);

	int magconOffset = 0;

	for(int i = 0; i < codeBlocks; i++)
	{
		h_infos[i].width = infos[i].width;
		h_infos[i].height = infos[i].height;
		h_infos[i].nominalWidth = infos[i].nominalWidth;
		h_infos[i].stripeNo = (int) ceil(infos[i].height / 4.0f);
		h_infos[i].subband = infos[i].subband;
		h_infos[i].magconOffset = magconOffset + infos[i].width;
		h_infos[i].magbits = infos[i].magbits;
		h_infos[i].coefficients = infos[i].coefficients;
		h_infos[i].compType = infos[i].compType;
		h_infos[i].dwtLevel = infos[i].dwtLevel;
		h_infos[i].stepSize = infos[i].stepSize;

		magconOffset += h_infos[i].width * (h_infos[i].stripeNo + 2);
	}

	GPU_JPEG2K::CoefficientState *d_stBuffors = (GPU_JPEG2K::CoefficientState *)mem_mg->alloc->dev(sizeof(GPU_JPEG2K::CoefficientState) * magconOffset, mem_mg->ctx);
	CHECK_ERRORS(cudaMemset((void *) d_stBuffors, 0, sizeof(GPU_JPEG2K::CoefficientState) * magconOffset));

	cuda_memcpy_htd(h_infos, d_infos, sizeof(CodeBlockAdditionalInfo) * codeBlocks);

//	cudaEvent_t start, end;
//	cudaEventCreate(&start);
//	cudaEventCreate(&end);

//	cudaEventRecord(start, 0);
//	printf("before launch encode: %d\n", stop_measure(start_bebcot));

//	long int start_ebcot = start_measure();
	if(targetSize == 0)
	{
		//printf("No pcrd\n");
		GPU_JPEG2K::launch_encode((int)ceil((double)codeBlocks/(double)THREADS), THREADS, d_stBuffors, d_outbuf, maxOutLength, d_infos, codeBlocks, mem_mg);
	}
	else
	{
		//printf("Pcrd\n");
		GPU_JPEG2K::launch_encode_pcrd((int)ceil((double)codeBlocks /(double)THREADS), THREADS, d_stBuffors, d_outbuf, maxOutLength, d_infos, codeBlocks, targetSize, mem_mg);
	}
	cudaThreadSynchronize();
//	printf("launch encode: %d\n", stop_measure(start_ebcot));
	CHECK_ERRORS();

//	cudaEventRecord(end, 0);
//	long int start_aebcot = start_measure();

//	long int start_copy = start_measure();
	cuda_memcpy_dth(d_infos, h_infos, sizeof(CodeBlockAdditionalInfo) * codeBlocks);
//	printf("copy: %d\n", stop_measure(start_copy));

	img->codestream = (byte *)mem_mg->alloc->host(sizeof(byte) * codeBlocks * maxOutLength, mem_mg->ctx);
	cuda_memcpy_dth(d_outbuf, img->codestream, sizeof(byte) * codeBlocks * maxOutLength);

//	long int start_cblk = start_measure();
	for(int i = 0; i < codeBlocks; i++)
	{
		infos[i].significantBits = h_infos[i].significantBits;
		infos[i].codingPasses = h_infos[i].codingPasses;

		if(h_infos[i].length > 0)
		{
			infos[i].length = h_infos[i].length;

			int len = h_infos[i].length;

			infos[i].codeStream = img->codestream + i * maxOutLength;
//			infos[i].codeStream = (byte *)mem_mg->alloc->host(sizeof(byte) * len, mem_mg->ctx);
//			cuda_memcpy_dth(d_outbuf + i * maxOutLength, infos[i].codeStream, sizeof(byte) * len);
		}
		else
		{
			infos[i].length = 0;
			infos[i].codeStream = NULL;
		}
	}
//	printf("cblk: %d\n", stop_measure(start_cblk));

//	long int start_free = start_measure();
//	cuda_d_free(d_outbuf);
	mem_mg->dealloc->dev(d_outbuf, mem_mg->ctx);
//	cuda_d_free(d_stBuffors);
	mem_mg->dealloc->dev(d_stBuffors, mem_mg->ctx);
//	cuda_d_free(d_infos);
	mem_mg->dealloc->dev(d_infos, mem_mg->ctx);

//	free(h_infos);
	mem_mg->dealloc->host(h_infos, mem_mg->ctx);

//	printf("free: %d\n", stop_measure(start_free));
	float elapsed = 0.0f;
//	cudaEventElapsedTime(&elapsed, start, end);
	
//	printf("after launch encode: %d\n", stop_measure(start_aebcot));
	return elapsed;
}

float gpuDecode(EntropyCodingTaskInfo *infos, mem_mg_t *mem_mg, int count)
{
	int codeBlocks = count;
	int maxOutLength = MAX_CODESTREAM_SIZE;

	int n = 0;
	for(int i = 0; i < codeBlocks; i++)
		n += infos[i].width * infos[i].height;

	byte *d_inbuf;
	GPU_JPEG2K::CoefficientState *d_stBuffors;

	CodeBlockAdditionalInfo *h_infos = (CodeBlockAdditionalInfo *)mem_mg->alloc->host(sizeof(CodeBlockAdditionalInfo) * codeBlocks, mem_mg->ctx);
	CodeBlockAdditionalInfo *d_infos;

	d_inbuf = (byte *)mem_mg->alloc->dev(sizeof(byte) * codeBlocks * maxOutLength, mem_mg->ctx);
	d_infos = (CodeBlockAdditionalInfo *)mem_mg->alloc->dev(sizeof(CodeBlockAdditionalInfo) * codeBlocks, mem_mg->ctx);

	int magconOffset = 0;

	for(int i = 0; i < codeBlocks; i++)
	{
		h_infos[i].width = infos[i].width;
		h_infos[i].height = infos[i].height;
		h_infos[i].nominalWidth = infos[i].nominalWidth;
		h_infos[i].stripeNo = (int) ceil(infos[i].height / 4.0f);
		h_infos[i].subband = infos[i].subband;
		h_infos[i].magconOffset = magconOffset + infos[i].width;
		h_infos[i].magbits = infos[i].magbits;
		h_infos[i].length = infos[i].length;
		h_infos[i].significantBits = infos[i].significantBits;

		h_infos[i].coefficients = (int *)mem_mg->alloc->dev(sizeof(int) * infos[i].nominalWidth * infos[i].nominalHeight, mem_mg->ctx);
		infos[i].coefficients = h_infos[i].coefficients;

		cuda_memcpy_htd(infos[i].codeStream, (void *) (d_inbuf + i * maxOutLength), sizeof(byte) * infos[i].length);

		magconOffset += h_infos[i].width * (h_infos[i].stripeNo + 2);
	}

	d_stBuffors = (GPU_JPEG2K::CoefficientState *)mem_mg->alloc->dev(sizeof(GPU_JPEG2K::CoefficientState) * magconOffset, mem_mg->ctx);
	cudaMemset((void *) d_stBuffors, 0, sizeof(GPU_JPEG2K::CoefficientState) * magconOffset);

	cuda_memcpy_htd(h_infos, d_infos, sizeof(CodeBlockAdditionalInfo) * codeBlocks);

//	cudaEvent_t start, end;
//	cudaEventCreate(&start);
//	cudaEventCreate(&end);
//
//	cudaEventRecord(start, 0);

	GPU_JPEG2K::launch_decode((int) ceil((float) codeBlocks / THREADS), THREADS, d_stBuffors, d_inbuf, maxOutLength, d_infos, codeBlocks);

//	cudaEventRecord(end, 0);

	mem_mg->dealloc->dev(d_inbuf, mem_mg->ctx);
	mem_mg->dealloc->dev(d_stBuffors, mem_mg->ctx);
	mem_mg->dealloc->dev(d_infos, mem_mg->ctx);

	mem_mg->dealloc->host(h_infos, mem_mg->ctx);

	float elapsed = 0.0f;
//	cudaEventElapsedTime(&elapsed, start, end);
	
	return elapsed;
}

void convert_to_task(EntropyCodingTaskInfo &task, const type_codeblock &cblk)
{
	task.coefficients = cblk.data_d;

	switch(cblk.parent_sb->orient)
	{
	case LL:
	case LH:
		task.subband = 0;
		break;
	case HL:
		task.subband = 1;
		break;
	case HH:
		task.subband = 2;
		break;
	}

	task.width = cblk.width;
	task.height = cblk.height;

	task.nominalWidth = cblk.parent_sb->parent_res_lvl->parent_tile_comp->cblk_w;
	task.nominalHeight = cblk.parent_sb->parent_res_lvl->parent_tile_comp->cblk_h;

	task.magbits = cblk.parent_sb->mag_bits;
	task.compType = cblk.parent_sb->parent_res_lvl->parent_tile_comp->parent_tile->parent_img->wavelet_type;
	task.dwtLevel = cblk.parent_sb->parent_res_lvl->dec_lvl_no;
	task.stepSize = cblk.parent_sb->step_size;
}

void extract_cblks(type_tile *tile, std::list<type_codeblock *> &out_cblks)
{
	for(unsigned int i = 0; i < tile->parent_img->num_components; i++)
	{
		type_tile_comp *tile_comp = &(tile->tile_comp[i]);
		for(unsigned int j = 0; j < tile_comp->num_rlvls; j++)
		{
			type_res_lvl *res_lvl = &(tile_comp->res_lvls[j]);
			for(unsigned int k = 0; k < res_lvl->num_subbands; k++)
			{
				type_subband *sb = &(res_lvl->subbands[k]);
				for(unsigned int l = 0; l < sb->num_cblks; l++)
					out_cblks.push_back(&(sb->cblks[l]));
			}
		}
	}
}

void binary_fprintf(FILE *file, unsigned int in)
{
	for(int i = 0; i < 32; i++)
		if((in >> (31 - i)) & 1)
			fprintf(file, "1");
		else
			fprintf(file, "0");

	fprintf(file, "\n");
}

void encode_tasks_serial(type_tile *tile) {
	type_coding_param *coding_params = tile->parent_img->coding_param;

	std::list<type_codeblock *> cblks;
	extract_cblks(tile, cblks);

	type_image *img = tile->parent_img;
	mem_mg_t *mem_mg = img->mem_mg;
	EntropyCodingTaskInfo *tasks = (EntropyCodingTaskInfo *)mem_mg->alloc->host(sizeof(EntropyCodingTaskInfo) * cblks.size(), mem_mg->ctx);

	std::list<type_codeblock *>::iterator ii = cblks.begin();

	int num_tasks = 0;
	for(; ii != cblks.end(); ++ii)
		convert_to_task(tasks[num_tasks++], *(*ii));

//	printf("%d\n", num_tasks);
	gpuEncode(tasks, img, num_tasks, coding_params->target_size);
//	printf("kernel consumption: %f\n", t);

	ii = cblks.begin();

	for(int i = 0; i < num_tasks; i++, ++ii)
	{
		(*ii)->codestream = tasks[i].codeStream;
		(*ii)->length = tasks[i].length;
		(*ii)->significant_bits = tasks[i].significantBits;
		(*ii)->num_coding_passes = tasks[i].codingPasses;
	}

//	free(tasks);
	mem_mg->dealloc->host(tasks, mem_mg->ctx);
}

void free_subband(type_tile *tile) {
	type_image *img = tile->parent_img;
	mem_mg_t *mem_mg = img->mem_mg;
	type_tile_comp *tile_comp;
	type_res_lvl *res_lvl;
	type_subband *sb;
	int i, j, k;

	for(i = 0; i < img->num_components; i++)
	{
		tile_comp = &(tile->tile_comp[i]);
		for(j = 0; j < tile_comp->num_rlvls; j++)
		{
			res_lvl = &(tile_comp->res_lvls[j]);
			for(k = 0; k < res_lvl->num_subbands; k++)
			{
				sb = &(res_lvl->subbands[k]);
				mem_mg->dealloc->dev(sb->cblks_data_d, mem_mg->ctx);
			}
		}
	}
}

void encode_tile(type_tile *tile)
{
//	println_start(INFO);

//	start_measure();

	encode_tasks_serial(tile);

	free_subband(tile);

//	stop_measure(INFO);

//	println_end(INFO);
}

void convert_to_decoding_task(EntropyCodingTaskInfo &task, const type_codeblock &cblk)
{
	switch(cblk.parent_sb->orient)
	{
	case LL:
	case LH:
		task.subband = 0;
		break;
	case HL:
		task.subband = 1;
		break;
	case HH:
		task.subband = 2;
		break;
	}

	task.width = cblk.width;
	task.height = cblk.height;

	task.nominalWidth = cblk.parent_sb->parent_res_lvl->parent_tile_comp->cblk_w;
	task.nominalHeight = cblk.parent_sb->parent_res_lvl->parent_tile_comp->cblk_h;

	task.magbits = cblk.parent_sb->mag_bits;

	task.codeStream = cblk.codestream;
	task.length = cblk.length;
	task.significantBits = cblk.significant_bits;

	//task.coefficients = cblk.data_d;
}

unsigned int reverse(unsigned int in)
{
	unsigned int out = 0;

	for(int i = 0; i < 32; i++)
	{
		out |= ((in >> i) & 1) << (31 - i);
	}

	return out;
}

void decode_tile(type_tile *tile)
{
//	println_start(INFO);

//	start_measure();

	mem_mg_t *mem_mg = tile->parent_img->mem_mg;
	std::list<type_codeblock *> cblks;
	extract_cblks(tile, cblks);

	EntropyCodingTaskInfo *tasks = (EntropyCodingTaskInfo *)mem_mg->alloc->host(sizeof(EntropyCodingTaskInfo) * cblks.size(), mem_mg->ctx);

	std::list<type_codeblock *>::iterator ii = cblks.begin();

	int num_tasks = 0;
	for(; ii != cblks.end(); ++ii)
	{
		convert_to_decoding_task(tasks[num_tasks++], *(*ii));
	}

//	printf("%d\n", num_tasks);

	gpuDecode(tasks, mem_mg, num_tasks);

	ii = cblks.begin();

	for(int i = 0; i < num_tasks; i++, ++ii)
	{
		(*ii)->data_d = tasks[i].coefficients;
	}

	mem_mg->dealloc->host(tasks, mem_mg->ctx);

//	stop_measure(INFO);

//	println_end(INFO);
}
