%/* $Id$ */

/* Description of FCOM protocol in XDR language.
 * We only use this for testing purposes since:
 *  - we want more efficient decoders/encoders.
 *  - we want slightly different layout for the
 *    C data structures.
 * However, we define these 'reference' encoders/decoders
 * for testing our versions.
 *
 * Author: Till Straumann <strauman@slac.stanford.edu>, 2009.
 */

#ifdef RPC_HDR
%#include <stdint.h>
%#include <fcom_api.h>
%/* For testing purposes we want a second minor version */
%#ifndef FCOM_PROTO_VERSION_12
%#define FCOM_PROTO_VERSION_12 FCOM_PROTO_CAT(FCOM_PROTO_MAJ_1,2)
%#endif
#endif

#ifdef RPC_XDR
%#define xdr_uint32_t xdr_u_int
#endif

%/* No point in transporting 16-bit integers.
% * XDR demands that these be encoded in 4 bytes
% */

enum FcomVersion {
	FCOM_P_VERSION_11 = FCOM_PROTO_VERSION_11,
	FCOM_P_VERSION_12 = FCOM_PROTO_VERSION_12
};

enum FcomType {
	FCOM_T_NONE   = FCOM_EL_NONE,
	FCOM_T_FLOAT  = FCOM_EL_FLOAT,
	FCOM_T_DOUBLE = FCOM_EL_DOUBLE,
	FCOM_T_UINT32 = FCOM_EL_UINT32,
	FCOM_T_INT32  = FCOM_EL_INT32,
	FCOM_T_INT8   = FCOM_EL_INT8
};

/* This is the definition of a data item/array;
 * we don't want to let rpcgen to emit a C-type
 * since we want a different layout.
 * We'd like arrays to be defined
 *
 *   struct Type_Arr {
 *        uint32_t   _fc_type_len;
 *        Type       _fc_type_val[];
 *   };
 *
 * Whereas rpcgen produces/uses
 *
 *   struct Type_Arr {
 *        uint32_t   _fc_type_len;
 *        Type      *_fc_type_val;
 *   };
 *
 *
 * However, the external representation of a FcomIt
 * is as defined here, in XDR language.
 */
#if defined(RPCGEN_TEST)
typedef uint32_t   FcomUint32<>;
typedef int32_t    FcomInt32 <>;
typedef opaque     FcomInt8  <>;
typedef float      FcomFloat <>;
typedef double     FcomDouble<>;
#elif defined(RPC_HDR)
%
%typedef struct FcomUint32 {
%    uint32_t FcomUint32_len;
%    uint32_t FcomUint32_val[];
%} FcomUint32;
% 
%typedef struct FcomInt32 {
%    uint32_t FcomInt32_len;
%    int32_t  FcomInt32_val[];
%} FcomInt32;
% 
%typedef struct FcomInt8 {
%    uint32_t FcomInt8_len;
%    int8_t   FcomInt8_val[];
%} FcomInt8;
% 
%typedef struct FcomFloat {
%    uint32_t FcomFloat_len;
%    float    FcomFloat_val[];
%} FcomFloat;
% 
%typedef struct FcomDouble {
%    uint32_t FcomDouble_len;
%    double   FcomDouble_val[];
%} FcomDouble;
% 
#endif

union FcomIt switch ( FcomType _type ) {
	case FCOM_T_UINT32:
		FcomUint32  _fc_u32;
	case FCOM_T_INT32:
		FcomInt32   _fc_i32;
	case FCOM_T_FLOAT:
		FcomFloat   _fc_flt;
	case FCOM_T_DOUBLE:
		FcomDouble  _fc_dbl;
	case FCOM_T_INT8:
		FcomInt8    _fc_i08;
};

struct FcomBlobV1_XDR_ {
	uint32_t     idnt;
	uint32_t     res3;
	uint32_t     tsHi;
	uint32_t     tsLo;
	uint32_t     stat;
	FcomIt       data;
};

union FcomBlob_XDR_ switch (FcomVersion vers) {
	case FCOM_P_VERSION_11:
	case FCOM_P_VERSION_12:
		FcomBlobV1_XDR_ _v1;
};

union FcomMsg_XDR_ switch (FcomVersion vers) {
	case FCOM_P_VERSION_11:
	case FCOM_P_VERSION_12:
		FcomBlob_XDR_ fcm_blobs<>;
};

#if defined(RPC_HDR)
%#define fcx_vers  vers
%#define fcx_idnt  idnt
%#define fcx_res3  res3
%#define fcx_tsHi  tsHi
%#define fcx_tsLo  tsLo
%#define fcx_stat  stat
%#define fcx_type  data._type
%#define fcx_nelm  data.FcomIt_u._fc_u32.FcomUint32_len

%#define fcx_u32   data.FcomIt_u._fc_u32.FcomUint32_val
%#define fcx_i32   data.FcomIt_u._fc_i32.FcomInt32_val
%#define fcx_i08   data.FcomIt_u._fc_i08.FcomInt8_val
%#define fcx_flt   data.FcomIt_u._fc_flt.FcomFloat_val
%#define fcx_dbl   data.FcomIt_u._fc_dbl.FcomDouble_val
#endif
