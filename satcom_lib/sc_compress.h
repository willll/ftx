/**
 *  SatCom Library
 *  by cafe-alpha.
 *  WWW: http://ppcenter.free.fr/satcom/
 *
 *  See LICENSE file for details.
**/

#ifndef SATCOM_COMPRESS_H
#define SATCOM_COMPRESS_H

#include "sc_common.h"



//----------------------------------------------------------------------
// Compression routines.
// 
//----------------------------------------------------------------------


/**
 * Compressed data's maximum header size.
 * unsigned char version, unsigned long decompressed_length
**/
#define SC_COMP_HEADER_MAXSIZE 5


#ifndef SC_COMP_DISABLE
/**
 * Compress data.
 *  ind : input data.
 *  inl : input data length.
 *  outd: output data.
 * At most inl+SC_COMP_HEADER_MAXSIZE bytes are required in the output buffer.
 * Return compressed data size.
**/
unsigned long comp_exec(unsigned char* ind, unsigned long inl, unsigned char* outd);
#endif // SC_COMP_DISABLE


#ifndef SC_DECOMP_DISABLE
/**
 * Decompress data.
 * It is supposed there is enough space to decompress the data.
 * Note : output data length is automatically computed from input data.
 *  ind : input data.
 *  inl : input data length.
 *  outd: output data.
 * Return decompressed data size.
**/
unsigned long decomp_exec(unsigned char* ind, unsigned long inl, unsigned char* outd);
/**
 * Get data size when decompressed.
 *  ind : input data.
 *  inl : input data length.
 * Return decompressed data size.
**/
unsigned long decomp_getsize(unsigned char* ind, unsigned long inl);
#endif // SC_DECOMP_DISABLE



#endif //SATCOM_COMPRESS_H
