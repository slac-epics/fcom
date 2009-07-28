/* $Id$ */
#ifndef FCOM_XDR_DEC_ENC_H
#define FCOM_XDR_DEC_ENC_H

#define __INSIDE_FCOM__
#include <fcom_api.h>

/* Decode a XDR-encoded blob into C-representation. The
 * user must provide pointers to the XDR stream and
 * the blob. 'avail' specifies the total amount of space
 * available to the C-representation of 'blob'.
 *
 * If the XDR representation is invalid the behavior is
 * undefined but not more than (avail+3)/4 32-bit words
 * are read from *xdr. 
 *
 * RETURNS: number of 32-bit words read from the XDR stream
 *          on success or an error code < 0 on failure.
 */
int
fcom_xdr_dec_blob(FcomBlobRef pb, int avail, uint32_t *xdr);

/* Encode a blob in C-representation into an XDR stream.
 * The user must provide a valid blob and a memory area
 * for the encoded stream, the size of which is 'avail'
 * bytes.
 * For convenience the GID of the blob is returned in
 * *p_gid (to extract the GID checking the protocol version
 * is necessary).
 *
 * RETURNS: number of 32-bit words encoded on success or
 *          error code < 0.
 */
int
fcom_xdr_enc_blob(uint32_t *xdr, FcomBlobRef pb, int avail, uint32_t *p_gid);

/* Peek at some parameters in a XDR encoded blob:
 *  - protocol version. If no match with a known/supported
 *    version is found then FCOM_ERR_BAD_VERSION is returned.
 *  - ID; returned in *p_id.
 *  - type and element count.
 * The total size of the C-representation of the blob is computed
 * and returned in *p_sz.
 *
 * RETURNS: number of 32-bit words in 'xdr' occupied by the blob
 *          (success) or an error code < 0 on failure.
 */
int
fcom_xdr_peek_size_id(int *p_sz, FcomID *p_id, uint32_t *xdr);

/********************************************
 * 'Messages' are XDR encoded 'FcomGroup's. *
 ********************************************/

/* Decode the 'message header' of an XDR-encoded group of
 * blobs.
 * Check message version and retrieve # of blobs.
 * RETURNS: number of decoded 32-bit words on success or a
 *          negative error code (e.g., version mismatch) 
 *          on error.
 */
int
fcom_xdr_dec_msghdr(uint32_t *xdrmem, int *p_nblobs);

/* Initialize a 'message' for encoding. The total space
 * available in *xdrmem in bytes is passed in 'size'.
 * The 'GID' of this group is passed in 'gid' - a GID
 * of FCOM_GID_ANY is acceptable in which case the
 * GID of the first blob will be used.
 *
 * RETURNS: number of 32-bit words encoded or negative
 *          error status (FCOM_ERR_INVALID_ID if GID
 *          is invalid).
 */
int
fcom_msg_init(uint32_t *xdrmem, uint16_t size, uint32_t gid);

/* Append a blob to the message. 'xdrmem' must be the
 * same pointer that had been passed to fcom_msg_init()
 * previously. 'Write-pointer' state information is
 * maintained internally.
 * 
 * RETURNS: number of 32-bit words encoded or negative
 *          error status on failure.
 */
int
fcom_msg_append_blob(uint32_t *xdrmem, FcomBlobRef pb);

/* Terminate encoding of a group into a complete message.
 * 'xdrmem' must be the same pointer that had been passed
 * to fcom_msg_init() previously.
 *
 * RETURNS: Total number of 32-bit words written to 'xdrmem'.
 *          Also, the GID and number of blobs contained
 *          in this message are returned in *p_gid and
 *          *p_nblobs, respectively.
 *
 * NOTE:    After executing this routine no more blobs
 *          may be appended (fcom_msg_append_blob()) to
 *          'xdrmem' which at this point is ready to
 *          be sent. However, the 'xdrmem' may be re-
 *          initialized (fcom_msg_init()) and then
 *          overwritten with fcom_msg_append_blob().
 */
uint32_t
fcom_msg_end(uint32_t *xdrmem, uint32_t *p_gid, uint32_t *p_nblobs);

/* For efficiency reasons:
 * concatenated fcom_msg_init/fcom_msg_append_blob/fcom_msg_end
 * in one call for a single blob.
 *
 * RETURNS: Number of 32-bit words encoded in 'xdrmem' on success
 *          or error status < 0 on failure.
 */
int
fcom_msg_one_blob(uint32_t *xdrmem, uint16_t sz, FcomBlobRef pb, uint32_t *p_gid);

#endif
