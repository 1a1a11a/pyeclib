#include <emmintrin.h>  //SSE2
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xor_code.h>

const int g_bit_lookup[] = {0x1, 0x2, 0x4, 0x8,
                                 0x10, 0x20, 0x40, 0x80,
                                 0x100, 0x200, 0x400, 0x800,
                                 0x1000, 0x2000, 0x4000, 0x8000,
                                 0x10000, 0x20000, 0x40000, 0x80000,
                                 0x100000, 0x200000, 0x400000, 0x800000,
                                 0x1000000, 0x2000000, 0x4000000, 0x8000000,
                                 0x10000000, 0x20000000, 0x40000000, 0x80000000};

int is_data_in_parity(int data_idx, int parity_bm)
{
  return ((g_bit_lookup[data_idx] & parity_bm) == g_bit_lookup[data_idx]);
}

int does_parity_have_data(int parity_idx, int data_bm)
{
  return ((g_bit_lookup[parity_idx] & data_bm) == g_bit_lookup[parity_idx]);
}

int parity_bit_lookup(xor_code_t *code_desc, int index)
{
  return g_bit_lookup[code_desc->k - index];
}

int data_bit_lookup(xor_code_t *code_desc, int index)
{
  return g_bit_lookup[index];
}

int missing_elements_bm(xor_code_t *code_desc, int *missing_elements, int (*bit_lookup_func)(xor_code_t *code_desc, int index))
{
  int i = 0;
  int bm = 0;

  while (missing_elements[i] > -1) {
    bm |= bit_lookup_func(code_desc, missing_elements[i]);
  } 

  return bm;
}

failure_pattern_t get_failure_pattern(xor_code_t *code_desc, int *missing_idxs)
{
  int i = 0;
  int num_failures = 0;
  failure_pattern_t pattern = FAIL_PATTERN_0D_0P;

  while (missing_idxs[i] > -1) {
    if (num_failures >= code_desc->hd) {
      pattern = FAIL_PATTERN_GE_HD;
    }
    switch(pattern) {
      case FAIL_PATTERN_0D_0P:
        pattern = (missing_idxs[i] < code_desc->k) ? FAIL_PATTERN_1D_0P : FAIL_PATTERN_0D_1P;
        break;
      case FAIL_PATTERN_1D_0P:
        pattern = (missing_idxs[i] < code_desc->k) ? FAIL_PATTERN_2D_0P : FAIL_PATTERN_1D_1P;
        break;
      case FAIL_PATTERN_2D_0P:
        pattern = (missing_idxs[i] < code_desc->k) ? FAIL_PATTERN_3D_0P : FAIL_PATTERN_2D_1P;
        break;
      case FAIL_PATTERN_3D_0P:
        pattern = FAIL_PATTERN_GE_HD; 
        break;
      case FAIL_PATTERN_1D_1P:
        pattern = (missing_idxs[i] < code_desc->k) ? FAIL_PATTERN_2D_1P : FAIL_PATTERN_1D_2P;
        break;
      case FAIL_PATTERN_1D_2P:
        pattern = FAIL_PATTERN_GE_HD; 
        break;
      case FAIL_PATTERN_2D_1P:
        pattern = FAIL_PATTERN_GE_HD; 
        break;
      case FAIL_PATTERN_0D_1P:
        pattern = (missing_idxs[i] < code_desc->k) ? FAIL_PATTERN_1D_1P : FAIL_PATTERN_0D_2P;
        break;
      case FAIL_PATTERN_0D_2P:
        pattern = (missing_idxs[i] < code_desc->k) ? FAIL_PATTERN_1D_2P : FAIL_PATTERN_0D_3P;
        break;
      case FAIL_PATTERN_0D_3P:
        pattern = FAIL_PATTERN_GE_HD; 
        break;
    } 
    if (pattern == FAIL_PATTERN_GE_HD) {
      break;
    }
    i++;
  }

  return pattern; 
}

void *aligned_malloc( size_t size, int align )
{
    void *mem = malloc( size + (align-1) + sizeof(void*) );
    char *amem;
    if (!mem) {
      return NULL;
    }

    amem = ((char*)mem) + sizeof(void*);
    amem += align - ((unsigned long)amem & (align - 1));

    ((void**)amem)[-1] = mem;
    return amem;
}

void aligned_free( void *mem )
{
    free( ((void**)mem)[-1] );
}

void fast_memcpy(char *dst, char *src, int size)
{
    // Use _mm_stream_si128((__m128i*) _buf2, sum);
    memcpy(dst, src, size);
}

/*
 * Buffers must be aligned to 16-byte boundaries
 *
 * Store in buf2 (opposite of memcpy convention...  Maybe change?)
 */
void xor_bufs_and_store(char *buf1, char *buf2, int blocksize)
{
  int residual_bytes = num_unaligned_end(blocksize);
  int fast_blocksize = blocksize > residual_bytes ? (blocksize - residual_bytes) : 0;
  int fast_int_blocksize = fast_blocksize / sizeof(__m128i);
  int i;
  __m128i *_buf1 = (__m128i*)buf1; 
  __m128i *_buf2 = (__m128i*)buf2; 
 
  /*
   * XOR aligned region using 128-bit XOR
   */
  for (i=0; i < fast_int_blocksize; i++) {
    _buf2[i] = _mm_xor_si128(_buf1[i], _buf2[i]);
  }

  /*
   * XOR unaligned end of region
   */
  for (i=fast_blocksize; i < blocksize; i++)
  {
    buf2[i] ^= buf1[i];
  }
}

void xor_code_encode(xor_code_t *code_desc, char **data, char **parity, int blocksize)
{
  int i, j;
  
  for (i=0; i < code_desc->k; i++) {
    for (j=0; j < code_desc->m; j++) {
      if (is_data_in_parity(i, code_desc->parity_bms[j])) {
        xor_bufs_and_store(data[i], parity[j], blocksize);
      }
    }
  }
}

void selective_encode(xor_code_t *code_desc, char **data, char **parity, int *missing_parity, int blocksize)
{
  int i = 0, j;
  int missing_parity_bm = 0;

  while (missing_parity[i] > -1) {
    missing_parity_bm |= ((1 << code_desc->k-missing_parity[i]));
    i++;
  }
  
  for (i=0; i < code_desc->k; i++) {
    for (j=0; j < code_desc->m; j++) {
      if (is_data_in_parity(i, code_desc->parity_bms[j]) && (missing_parity_bm & (1 << j) == (1 << j))) {
        xor_bufs_and_store(data[i], parity[j], blocksize);
      }
    }
  }
}

int * get_missing_parity(xor_code_t *code_desc, int *missing_idxs)
{
  int *missing_parity = (int*)malloc(sizeof(int)*MAX_PARITY);
  int i = 0, j = 0;

  while (missing_idxs[i] > -1) {
    if (missing_idxs[i] >= code_desc->k) {
      missing_parity[j] = missing_idxs[i]; 
      j++;
    }
    i++;
  }
  
  missing_parity[j] = -1;
  return missing_parity;
}

int * get_missing_data(xor_code_t *code_desc, int *missing_idxs)
{
  int *missing_data = (int*)malloc(sizeof(int)*MAX_DATA);
  int i = 0, j = 0;

  while (missing_idxs[i] > -1) {
    if (missing_idxs[i] < code_desc->k) {
      missing_data[j] = missing_idxs[i]; 
      j++;
    }
    i++;
  }
  
  missing_data[j] = -1;
  return missing_data;
}

int num_missing_data_in_parity(xor_code_t *code_desc, int parity_idx, int *missing_data)
{
  int i = 0;
  int num_missing_data = 0;
  int relative_parity_index = parity_idx - code_desc->k;
  if (missing_data == NULL) {
    return 0;
  }

  while (missing_data[i] > -1) {
    if (does_parity_have_data(relative_parity_index, code_desc->data_bms[missing_data[i]]) > 0) {
      num_missing_data++;
    }
    i++;
  }
  
  return num_missing_data;
}

int index_of_connected_parity(xor_code_t *code_desc, int data_index, int *missing_parity, int *missing_data)
{
  int parity_index = -1;
  int i;
  
  for (i=0; i < code_desc->m; i++) {
    if (num_missing_data_in_parity(code_desc, i + code_desc->k, missing_data) > 1) {
      continue;
    }
    if (is_data_in_parity(data_index, code_desc->parity_bms[i])) {
      int j=0;
      int is_missing = 0;
      if (missing_parity == NULL) {
        parity_index = i;
        break;
      }
      while (missing_parity[j] > -1) {
        if ((code_desc->k + i) == missing_parity[j]) {
          is_missing = 1; 
          break; 
        }
      }
      if (!is_missing) {
        parity_index = i;
        break;
      }
    }
  }
  
  // Must add k to get the absolute
  // index of the parity in the stripe
  return parity_index > -1 ? parity_index + code_desc->k : parity_index;
}

void remove_from_missing_list(int element, int *missing_list)
{
  int i = 0;
  int elem_idx = -1;
  int num_elems = 0;
  
  while (missing_list[i] > -1) {
    if (missing_list[i] == element) {
      elem_idx = i;
      missing_list[i] = -1;
    }
    i++;
  }

  num_elems = i;

  for (i=elem_idx;i < num_elems-1;i++) {
    int tmp = missing_list[i+1]; 
    missing_list[i+1] = missing_list[i];
    missing_list[i] = tmp;
  }
}

