/* parsing packets: formats and tools
 * Copyright (C) 1997 Angelos D. Keromytis.
 * Copyright (C) 1998-2001,2013-2014  D. Hugh Redelmeier.
 * Copyright (C) 2012 Avesh Agarwal <avagarwa@redhat.com>
 * Copyright (C) 2012 Paul Wouters <pwouters@redhat.com>
 * Copyright (C) 2005-2017 Michael Richardson <mcr@xelerance.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <netinet/in.h>
#include <string.h>

#include <openswan.h>

#include "constants.h"
#include "oswlog.h"

#include "packet.h"

/* ISAKMP Header: for all messages
 * layout from RFC 2408 "ISAKMP" section 3.1
 *                      1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !                          Initiator                            !
 * !                            Cookie                             !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !                          Responder                            !
 * !                            Cookie                             !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !  Next Payload ! MjVer ! MnVer ! Exchange Type !     Flags     !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !                          Message ID                           !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !                            Length                             !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

static field_desc isa_fields[] = {
    { ft_raw, COOKIE_SIZE, "initiator cookie", NULL },
    { ft_raw, COOKIE_SIZE, "responder cookie", NULL },
    { ft_np_in,8/BITS_PER_BYTE, "next payload type", &payload_names },
    { ft_enum, 8/BITS_PER_BYTE, "ISAKMP version", &version_names },
    { ft_enum, 8/BITS_PER_BYTE, "exchange type", &exchange_names },
    { ft_set, 8/BITS_PER_BYTE, "flags", flag_bit_names },
    { ft_raw, 32/BITS_PER_BYTE, "message ID", NULL },
    { ft_len, 32/BITS_PER_BYTE, "length", NULL },
    { ft_end, 0, NULL, NULL }
};

struct_desc isakmp_hdr_desc = { "ISAKMP Message", isa_fields, sizeof(struct isakmp_hdr) };

/* Generic portion of all ISAKMP payloads.
 * layout from RFC 2408 "ISAKMP" section 3.2
 * This describes the first 32-bit chunk of all payloads.
 * The previous next payload depends on the actual payload type.
 *                      1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * ! Next Payload  !   RESERVED    !         Payload Length        !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

static field_desc isag_fields[] = {
    { ft_np,  8/BITS_PER_BYTE, "next payload type", &payload_names },
    { ft_mbz, 8/BITS_PER_BYTE, NULL, NULL },
    { ft_len, 16/BITS_PER_BYTE, "length", NULL },
    { ft_end, 0, NULL, NULL }
};

struct_desc isakmp_generic_desc = { "ISAKMP Generic Payload", isag_fields, sizeof(struct isakmp_generic) };


/* ISAKMP Data Attribute (generic representation within payloads)
 * layout from RFC 2408 "ISAKMP" section 3.3
 * This is not a payload type.
 * In TLV format, this is followed by a value field.
 *                      1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !A!       Attribute Type        !    AF=0  Attribute Length     !
 * !F!                             !    AF=1  Attribute Value      !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * .                   AF=0  Attribute Value                       .
 * .                   AF=1  Not Transmitted                       .
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

/* Oakley Attributes */
static field_desc isaat_fields_oakley[] = {
    { ft_af_enum, 16/BITS_PER_BYTE, "af+type", &oakley_attr_names },
    { ft_lv, 16/BITS_PER_BYTE, "length/value", NULL },
    { ft_end, 0, NULL, NULL }
};

struct_desc isakmp_oakley_attribute_desc = {
    "ISAKMP Oakley attribute",
    isaat_fields_oakley, sizeof(struct isakmp_attribute) };

/* IPsec DOI Attributes */
static field_desc isaat_fields_ipsec[] = {
    { ft_af_enum, 16/BITS_PER_BYTE, "af+type", &ipsec_attr_names },
    { ft_lv, 16/BITS_PER_BYTE, "length/value", NULL },
    { ft_end, 0, NULL, NULL }
};

struct_desc isakmp_ipsec_attribute_desc = {
    "ISAKMP IPsec DOI attribute",
    isaat_fields_ipsec, sizeof(struct isakmp_attribute) };

/* XAUTH Attributes */
static field_desc isaat_fields_xauth[] = {
    { ft_af_loose_enum, 16/BITS_PER_BYTE, "ModeCfg attr type", &modecfg_attr_names },
    { ft_lv, 16/BITS_PER_BYTE, "length/value", NULL },
    { ft_end, 0, NULL, NULL }
};

struct_desc isakmp_xauth_attribute_desc = {
    "ISAKMP ModeCfg attribute",
    isaat_fields_xauth, sizeof(struct isakmp_attribute) };

/* ISAKMP Security Association Payload
 * layout from RFC 2408 "ISAKMP" section 3.4
 * A variable length Situation follows.
 * Previous next payload: ISAKMP_NEXT_SA
 *                      1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * ! Next Payload  !   RESERVED    !         Payload Length        !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !              Domain of Interpretation  (DOI)                  !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !                                                               !
 * ~                           Situation                           ~
 * !                                                               !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
static field_desc isasa_fields[] = {
    { ft_np,  8/BITS_PER_BYTE, "next payload type", &payload_names },
    { ft_mbz, 8/BITS_PER_BYTE, NULL, NULL },
    { ft_len, 16/BITS_PER_BYTE, "length", NULL },
    { ft_enum, 32/BITS_PER_BYTE, "DOI", &doi_names },
    { ft_end, 0, NULL, NULL }
};

struct_desc isakmp_sa_desc = { "ISAKMP Security Association Payload", isasa_fields, sizeof(struct isakmp_sa) };

static field_desc ipsec_sit_field[] = {
    { ft_set, 32/BITS_PER_BYTE, "IPsec DOI SIT", &sit_bit_names },
    { ft_end, 0, NULL, NULL }
};

struct_desc ipsec_sit_desc = { "IPsec DOI SIT", ipsec_sit_field, sizeof(u_int32_t) };

/* ISAKMP Proposal Payload
 * layout from RFC 2408 "ISAKMP" section 3.5
 * A variable length SPI follows.
 * Previous next payload: ISAKMP_NEXT_P
 *                      1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * ! Next Payload  !   RESERVED    !         Payload Length        !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !  Proposal #   !  Protocol-Id  !    SPI Size   !# of Transforms!
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !                        SPI (variable)                         !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
static field_desc isap_fields[] = {
    { ft_np,  8/BITS_PER_BYTE, "next payload type", &payload_names },
    { ft_mbz, 8/BITS_PER_BYTE, NULL, NULL },
    { ft_len, 16/BITS_PER_BYTE, "length", NULL },
    { ft_nat, 8/BITS_PER_BYTE, "proposal number", NULL },
    { ft_enum, 8/BITS_PER_BYTE, "protocol ID", &protocol_names },
    { ft_nat, 8/BITS_PER_BYTE, "SPI size", NULL },
    { ft_nat, 8/BITS_PER_BYTE, "number of transforms", NULL },
    { ft_end, 0, NULL, NULL }
};

struct_desc b = { "ISAKMP Proposal Payload", isap_fields, sizeof(struct isakmp_proposal) };

/* ISAKMP Transform Payload
 * layout from RFC 2408 "ISAKMP" section 3.6
 * Variable length SA Attributes follow.
 * Previous next payload: ISAKMP_NEXT_T
 *                      1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * ! Next Payload  !   RESERVED    !         Payload Length        !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !  Transform #  !  Transform-Id !           RESERVED2           !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !                                                               !
 * ~                        SA Attributes                          ~
 * !                                                               !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

/* PROTO_ISAKMP */
static field_desc isat_fields_isakmp[] = {
    { ft_np,  8/BITS_PER_BYTE, "next payload type", &payload_names },
    { ft_mbz, 8/BITS_PER_BYTE, NULL, NULL },
    { ft_len, 16/BITS_PER_BYTE, "length", NULL },
    { ft_nat, 8/BITS_PER_BYTE, "transform number", NULL },
    { ft_enum, 8/BITS_PER_BYTE, "transform ID", &isakmp_transformid_names },
    { ft_mbz, 16/BITS_PER_BYTE, NULL, NULL },
    { ft_end, 0, NULL, NULL }
};

struct_desc isakmp_isakmp_transform_desc = {
    "ISAKMP Transform Payload (ISAKMP)",
    isat_fields_isakmp, sizeof(struct isakmp_transform) };

/* PROTO_IPSEC_AH */
static field_desc isat_fields_ah[] = {
    { ft_np,  8/BITS_PER_BYTE, "next payload type", &payload_names },
    { ft_mbz, 8/BITS_PER_BYTE, NULL, NULL },
    { ft_len, 16/BITS_PER_BYTE, "length", NULL },
    { ft_nat, 8/BITS_PER_BYTE, "transform number", NULL },
    { ft_enum, 8/BITS_PER_BYTE, "transform ID", &ah_transformid_names },
    { ft_mbz, 16/BITS_PER_BYTE, NULL, NULL },
    { ft_end, 0, NULL, NULL }
};

struct_desc isakmp_ah_transform_desc = {
    "ISAKMP Transform Payload (AH)",
    isat_fields_ah, sizeof(struct isakmp_transform) };

/* PROTO_IPSEC_ESP */
static field_desc isat_fields_esp[] = {
    { ft_np,  8/BITS_PER_BYTE, "next payload type", &payload_names },
    { ft_mbz, 8/BITS_PER_BYTE, NULL, NULL },
    { ft_len, 16/BITS_PER_BYTE, "length", NULL },
    { ft_nat, 8/BITS_PER_BYTE, "transform number", NULL },
    { ft_enum, 8/BITS_PER_BYTE, "transform ID", &esp_transformid_names },
    { ft_mbz, 16/BITS_PER_BYTE, NULL, NULL },
    { ft_end, 0, NULL, NULL }
};

struct_desc isakmp_esp_transform_desc = {
    "ISAKMP Transform Payload (ESP)",
    isat_fields_esp, sizeof(struct isakmp_transform) };

/* PROTO_IPCOMP */
static field_desc isat_fields_ipcomp[] = {
    { ft_np,  8/BITS_PER_BYTE, "next payload type", &payload_names },
    { ft_mbz, 8/BITS_PER_BYTE, NULL, NULL },
    { ft_len, 16/BITS_PER_BYTE, "length", NULL },
    { ft_nat, 8/BITS_PER_BYTE, "transform number", NULL },
    { ft_enum, 8/BITS_PER_BYTE, "transform ID", &ipcomp_transformid_names },
    { ft_mbz, 16/BITS_PER_BYTE, NULL, NULL },
    { ft_end, 0, NULL, NULL }
};

struct_desc isakmp_ipcomp_transform_desc = {
    "ISAKMP Transform Payload (COMP)",
    isat_fields_ipcomp, sizeof(struct isakmp_transform) };


/* ISAKMP Key Exchange Payload: no fixed fields beyond the generic ones.
 * layout from RFC 2408 "ISAKMP" section 3.7
 * Variable Key Exchange Data follow the generic fields.
 * Previous next payload: ISAKMP_NEXT_KE
 *                      1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * ! Next Payload  !   RESERVED    !         Payload Length        !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !                                                               !
 * ~                       Key Exchange Data                       ~
 * !                                                               !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
struct_desc isakmp_keyex_desc = { "ISAKMP Key Exchange Payload", isag_fields, sizeof(struct isakmp_generic) };

/* ISAKMP Identification Payload
 * layout from RFC 2408 "ISAKMP" section 3.8
 * See "struct identity" declared later.
 * Variable length Identification Data follow.
 * Previous next payload: ISAKMP_NEXT_ID
 *                      1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * ! Next Payload  !   RESERVED    !         Payload Length        !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !   ID Type     !             DOI Specific ID Data              !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !                                                               !
 * ~                   Identification Data                         ~
 * !                                                               !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
static field_desc isaid_fields[] = {
    { ft_np,  8/BITS_PER_BYTE, "next payload type", &payload_names },
    { ft_mbz, 8/BITS_PER_BYTE, NULL, NULL },
    { ft_len, 16/BITS_PER_BYTE, "length", NULL },
    { ft_enum, 8/BITS_PER_BYTE, "ID type", &ident_names },	/* ??? depends on DOI? */
    { ft_nat, 8/BITS_PER_BYTE, "DOI specific A", NULL },	/* ??? depends on DOI? */
    { ft_nat, 16/BITS_PER_BYTE, "DOI specific B", NULL },	/* ??? depends on DOI? */
    { ft_end, 0, NULL, NULL }
};

struct_desc isakmp_identification_desc = { "ISAKMP Identification Payload", isaid_fields, sizeof(struct isakmp_id) };

/* IPSEC Identification Payload Content
 * layout from RFC 2407 "IPsec DOI" section 4.6.2
 * See struct isakmp_id declared earlier.
 * Note: Hashing skips the ISAKMP generic payload header
 * Variable length Identification Data follow.
 *                      1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !  Next Payload !   RESERVED    !        Payload Length         !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !   ID Type     !  Protocol ID  !             Port              !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * ~                     Identification Data                       ~
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
static field_desc isaiid_fields[] = {
    { ft_np,  8/BITS_PER_BYTE, "next payload type", &payload_names },
    { ft_mbz, 8/BITS_PER_BYTE, NULL, NULL },
    { ft_len, 16/BITS_PER_BYTE, "length", NULL },
    { ft_enum, 8/BITS_PER_BYTE, "ID type", &ident_names },
    { ft_nat, 8/BITS_PER_BYTE, "Protocol ID", NULL },	/* ??? UDP/TCP or 0? */
    { ft_nat, 16/BITS_PER_BYTE, "port", NULL },
    { ft_end, 0, NULL, NULL }
};

struct_desc isakmp_ipsec_identification_desc = { "ISAKMP Identification Payload (IPsec DOI)", isaiid_fields, sizeof(struct isakmp_ipsec_id) };

/* ISAKMP Certificate Payload: oddball fixed field beyond the generic ones.
 * layout from RFC 2408 "ISAKMP" section 3.9
 * Variable length Certificate Data follow the generic fields.
 * Previous next payload: ISAKMP_NEXT_CERT.
 *                      1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * ! Next Payload  !   RESERVED    !         Payload Length        !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * ! Cert Encoding !                                               !
 * +-+-+-+-+-+-+-+-+                                               !
 * ~                       Certificate Data                        ~
 * !                                                               !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
static field_desc isacert_fields[] = {
    { ft_np,  8/BITS_PER_BYTE, "next payload type", &payload_names },
    { ft_mbz, 8/BITS_PER_BYTE, NULL, NULL },
    { ft_len, 16/BITS_PER_BYTE, "length", NULL },
    { ft_enum, 8/BITS_PER_BYTE, "cert encoding", &cert_type_names },
    { ft_end, 0, NULL, NULL }
};

/* Note: the size field of isakmp_ipsec_certificate_desc cannot be
 * sizeof(struct isakmp_cert) because that will rounded up for padding.
 */
 struct_desc isakmp_ipsec_certificate_desc = { "ISAKMP Certificate Payload", isacert_fields, ISAKMP_CERT_SIZE };
/* ISAKMP Certificate Request Payload: oddball field beyond the generic ones.
 * layout from RFC 2408 "ISAKMP" section 3.10
 * Variable length Certificate Types and Certificate Authorities follow.
 * Previous next payload: ISAKMP_NEXT_CR.
 *                      1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * ! Next Payload  !   RESERVED    !         Payload Length        !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !  Cert. Type   !                                               !
 * +-+-+-+-+-+-+-+-+                                               !
 * ~                    Certificate Authority                      ~
 * !                                                               !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
static field_desc isacr_fields[] = {
    { ft_np,   8/BITS_PER_BYTE, "next payload type", &payload_names },
    { ft_mbz, 8/BITS_PER_BYTE, NULL, NULL },
    { ft_len, 16/BITS_PER_BYTE, "length", NULL },
    { ft_enum, 8/BITS_PER_BYTE, "cert type", &cert_type_names },
    { ft_end, 0, NULL, NULL }
};

/* Note: the size field of isakmp_ipsec_cert_req_desc cannot be
 * sizeof(struct isakmp_cr) because that will rounded up for padding.
 */
struct_desc isakmp_ipsec_cert_req_desc = { "ISAKMP Certificate RequestPayload", isacr_fields, ISAKMP_CR_SIZE };

/* ISAKMP Hash Payload: no fixed fields beyond the generic ones.
 * layout from RFC 2408 "ISAKMP" section 3.11
 * Variable length Hash Data follow.
 * Previous next payload: ISAKMP_NEXT_HASH.
 *                      1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * ! Next Payload  !   RESERVED    !         Payload Length        !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !                                                               !
 * ~                           Hash Data                           ~
 * !                                                               !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
struct_desc isakmp_hash_desc = { "ISAKMP Hash Payload", isag_fields, sizeof(struct isakmp_generic) };

/* ISAKMP Signature Payload: no fixed fields beyond the generic ones.
 * layout from RFC 2408 "ISAKMP" section 3.12
 * Variable length Signature Data follow.
 * Previous next payload: ISAKMP_NEXT_SIG.
 *                      1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * ! Next Payload  !   RESERVED    !         Payload Length        !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !                                                               !
 * ~                         Signature Data                        ~
 * !                                                               !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
struct_desc isakmp_signature_desc = { "ISAKMP Signature Payload", isag_fields, sizeof(struct isakmp_generic) };

/* ISAKMP Nonce Payload: no fixed fields beyond the generic ones.
 * layout from RFC 2408 "ISAKMP" section 3.13
 * Variable length Nonce Data follow.
 * Previous next payload: ISAKMP_NEXT_NONCE.
 *                      1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * ! Next Payload  !   RESERVED    !         Payload Length        !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !                                                               !
 * ~                            Nonce Data                         ~
 * !                                                               !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
struct_desc isakmp_nonce_desc = { "ISAKMP Nonce Payload", isag_fields, sizeof(struct isakmp_generic) };

/* ISAKMP Notification Payload
 * layout from RFC 2408 "ISAKMP" section 3.14
 * This is followed by a variable length SPI
 * and then possibly by variable length Notification Data.
 * Previous next payload: ISAKMP_NEXT_N
 *                      1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * ! Next Payload  !   RESERVED    !         Payload Length        !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !              Domain of Interpretation  (DOI)                  !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !  Protocol-ID  !   SPI Size    !      Notify Message Type      !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !                                                               !
 * ~                Security Parameter Index (SPI)                 ~
 * !                                                               !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !                                                               !
 * ~                       Notification Data                       ~
 * !                                                               !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
static field_desc isan_fields[] = {
    { ft_np,  8/BITS_PER_BYTE, "next payload type", &payload_names },
    { ft_mbz, 8/BITS_PER_BYTE, NULL, NULL },
    { ft_len, 16/BITS_PER_BYTE, "length", NULL },
    { ft_enum, 32/BITS_PER_BYTE, "DOI", &doi_names },
    { ft_nat, 8/BITS_PER_BYTE, "protocol ID", NULL },	/* ??? really enum: ISAKMP, IPSEC, ESP, ... */
    { ft_nat, 8/BITS_PER_BYTE, "SPI size", NULL },
    { ft_enum, 16/BITS_PER_BYTE, "Notify Message Type", &ipsec_notification_names },
    { ft_end, 0, NULL, NULL }
};

struct_desc isakmp_notification_desc = { "ISAKMP Notification Payload", isan_fields, sizeof(struct isakmp_notification) };

/* ISAKMP Delete Payload
 * layout from RFC 2408 "ISAKMP" section 3.15
 * This is followed by a variable length SPI.
 * Previous next payload: ISAKMP_NEXT_D
 *                      1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * ! Next Payload  !   RESERVED    !         Payload Length        !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !              Domain of Interpretation  (DOI)                  !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !  Protocol-Id  !   SPI Size    !           # of SPIs           !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !                                                               !
 * ~               Security Parameter Index(es) (SPI)              ~
 * !                                                               !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
static field_desc isad_fields[] = {
    { ft_np,  8/BITS_PER_BYTE, "next payload type", &payload_names },
    { ft_mbz, 8/BITS_PER_BYTE, NULL, NULL },
    { ft_len, 16/BITS_PER_BYTE, "length", NULL },
    { ft_enum, 32/BITS_PER_BYTE, "DOI", &doi_names },
    { ft_nat, 8/BITS_PER_BYTE, "protocol ID", NULL },	/* ??? really enum: ISAKMP, IPSEC */
    { ft_nat, 8/BITS_PER_BYTE, "SPI size", NULL },
    { ft_nat, 16/BITS_PER_BYTE, "number of SPIs", NULL },
    { ft_end, 0, NULL, NULL }
};

struct_desc isakmp_delete_desc = { "ISAKMP Delete Payload", isad_fields, sizeof(struct isakmp_delete) };

/* ISAKMP Vendor ID Payload
 * layout from RFC 2408 "ISAKMP" section 3.15
 * This is followed by a variable length VID.
 * Previous next payload: ISAKMP_NEXT_VID
 *                      1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * ! Next Payload  !   RESERVED    !         Payload Length        !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !                                                               !
 * ~                        Vendor ID (VID)                        ~
 * !                                                               !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
struct_desc isakmp_vendor_id_desc = { "ISAKMP Vendor ID Payload", isag_fields, sizeof(struct isakmp_generic) };

/* MODECFG */
/*
 * From draft-dukes-ike-mode-cfg
3.2. Attribute Payload
                           1                   2                   3
       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     ! Next Payload  !   RESERVED    !         Payload Length        !
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     !     Type      !   RESERVED    !           Identifier          !
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     !                                                               !
     ~                           Attributes                          ~
     !                                                               !
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
static field_desc isaattr_fields[] = {
    { ft_np,  8/BITS_PER_BYTE, "next payload type", &payload_names },
    { ft_mbz, 8/BITS_PER_BYTE, NULL, NULL },
    { ft_len, 16/BITS_PER_BYTE, "length", NULL },
    { ft_enum, 8/BITS_PER_BYTE, "Attr Msg Type", &attr_msg_type_names },
    { ft_mbz, 8/BITS_PER_BYTE, NULL, NULL },
    { ft_nat, 16/BITS_PER_BYTE, "Identifier", NULL },
    { ft_end, 0, NULL, NULL }
};

/* MODECFG */
/* From draft-dukes-ike-mode-cfg
3.2. Attribute Payload
                           1                   2                   3
       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     ! Next Payload  !   RESERVED    !         Payload Length        !
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     !     Type      !   RESERVED    !           Identifier          !
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     !                                                               !
     !                                                               !
     ~                           Attributes                          ~
     !                                                               !
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/

struct_desc isakmp_attr_desc = { "ISAKMP Mode Attribute", isaattr_fields, sizeof(struct isakmp_mode_attr) };

/* ISAKMP NAT-Traversal NAT-D
 * layout from draft-ietf-ipsec-nat-t-ike-01.txt section 3.2
 *
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * ! Next Payload  !   RESERVED    !         Payload Length        !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !                 HASH of the address and port                  !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
struct_desc isakmp_nat_d = { "ISAKMP NAT-D Payload", isag_fields, sizeof(struct isakmp_generic) };

/* ISAKMP NAT-Traversal NAT-OA
 * layout from draft-ietf-ipsec-nat-t-ike-01.txt section 4.2
 *
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * ! Next Payload  !   RESERVED    !         Payload Length        !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !   ID Type     !   RESERVED    !            RESERVED           !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !         IPv4 (4 octets) or IPv6 address (16 octets)           !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
static field_desc isanat_oa_fields[] = {
    { ft_np,  8/BITS_PER_BYTE, "next payload type", &payload_names },
    { ft_zig, 8/BITS_PER_BYTE, NULL, NULL },
    { ft_len, 16/BITS_PER_BYTE, "length", NULL },
    { ft_enum, 8/BITS_PER_BYTE, "ID type", &ident_names },
    { ft_zig, 24/BITS_PER_BYTE, NULL, NULL },
    { ft_end, 0, NULL, NULL }
};

struct_desc isakmp_nat_oa = { "ISAKMP NAT-OA Payload", isanat_oa_fields, sizeof(struct isakmp_nat_oa) };

/*
 * GENERIC IKEv2 header.
 * Note differs from IKEv1, in that it has a critical bit
 */
static field_desc ikev2generic_fields[] = {
    { ft_np,  8/BITS_PER_BYTE, "next payload type", &payload_names },
    { ft_set, 8/BITS_PER_BYTE, "critical bit", critical_names},
    { ft_len, 16/BITS_PER_BYTE, "length", NULL },
    { ft_end,  0, NULL, NULL }
};
struct_desc ikev2_generic_desc = { "IKEv2 Generic Payload",
				   ikev2generic_fields,
				   sizeof(struct ikev2_generic) };

/*
 * IKEv2 - Security Association Payload
 *
 * layout from RFC 4306 - section 3.3.
 * A variable number of proposals follows.
 *
 *                         1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    ! Next Payload  !C!  RESERVED   !         Payload Length        !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    !                                                               !
 *    ~                          <Proposals>                          ~
 *    !                                                               !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 */
struct_desc ikev2_sa_desc = { "IKEv2 Security Association Payload",
			      ikev2generic_fields, sizeof(struct ikev2_sa) };


/* IKEv2 - Proposal sub-structure
 *
 * 3.3.1.  Proposal Substructure
 *
 *
 *                         1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    ! 0 (last) or 2 !   RESERVED    !         Proposal Length       !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    ! Proposal #    !  Protocol ID  !    SPI Size   !# of Transforms!
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    ~                        SPI (variable)                         ~
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    !                                                               !
 *    ~                        <Transforms>                           ~
 *    !                                                               !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *             Figure 7:  Proposal Substructure
 */
static field_desc ikev2prop_fields[] = {
    { ft_np,   8/BITS_PER_BYTE, "next payload type", &payload_names },
    { ft_zig,  8/BITS_PER_BYTE, NULL, NULL },
    { ft_len, 16/BITS_PER_BYTE, "length", NULL },
    { ft_nat,  8/BITS_PER_BYTE, "prop #", NULL },
    { ft_nat,  8/BITS_PER_BYTE, "proto ID", NULL },
    { ft_nat,  8/BITS_PER_BYTE, "spi size", NULL },
    { ft_nat,  8/BITS_PER_BYTE, "# transforms", NULL },
    { ft_end,  0, NULL, NULL }
};

struct_desc ikev2_prop_desc = { "IKEv2 Proposal Substructure Payload",
			      ikev2prop_fields, sizeof(struct ikev2_prop) };


/*
 * 3.3.2.  Transform Substructure
 *
 *                         1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    ! 0 (last) or 3 !   RESERVED    !        Transform Length       !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    !Transform Type !   RESERVED    !          Transform ID         !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    !                                                               !
 *    ~                      Transform Attributes                     ~
 *    !                                                               !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 */
static field_desc ikev2trans_fields[] = {
    { ft_np,   8/BITS_PER_BYTE, "next payload type", &payload_names },
    { ft_zig,  8/BITS_PER_BYTE, NULL, NULL },
    { ft_len, 16/BITS_PER_BYTE, "length", NULL },
    { ft_nat,  8/BITS_PER_BYTE, "transform type", &trans_type_names },
    { ft_zig,  8/BITS_PER_BYTE, NULL, NULL },
    { ft_nat, 16/BITS_PER_BYTE, "transform ID", NULL },
    { ft_end,  0, NULL, NULL }
};

struct_desc ikev2_trans_desc = { "IKEv2 Transform Substructure Payload",
			      ikev2trans_fields, sizeof(struct ikev2_trans) };

/*
 * 3.3.5.   [Transform] Attribute substructure
 *
 *                          1                   2                   3
 *      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     !A!       Attribute Type        !    AF=0  Attribute Length     !
 *     !F!                             !    AF=1  Attribute Value      !
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     !                   AF=0  Attribute Value                       !
 *     !                   AF=1  Not Transmitted                       !
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 */
static field_desc ikev2_trans_attr_fields[] = {
    { ft_af_enum, 16/BITS_PER_BYTE, "af+type", &ikev2_trans_attr_descs },
    { ft_lv,      16/BITS_PER_BYTE, "length/value", NULL },
    { ft_end,     0, NULL, NULL }
};

struct_desc ikev2_trans_attr_desc = {
    "IKEv2 Attribute Substructure Payload",
    ikev2_trans_attr_fields, sizeof(struct ikev2_trans_attr) };

/* 3.4.  Key Exchange Payload
 *
 * The Key Exchange Payload, denoted KE in this memo, is used to
 * exchange Diffie-Hellman public numbers as part of a Diffie-Hellman
 * key exchange.  The Key Exchange Payload consists of the IKE generic
 * payload header followed by the Diffie-Hellman public value itself.
 *
 *                         1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    ! Next Payload  !C!  RESERVED   !         Payload Length        !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    !          DH Group #           !           RESERVED            !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    !                                                               !
 *    ~                       Key Exchange Data                       ~
 *    !                                                               !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *              Figure 10:  Key Exchange Payload Format
 *
 */
static field_desc ikev2ke_fields[] = {
    { ft_np,  8/BITS_PER_BYTE, "next payload type", &payload_names },
    { ft_set, 8/BITS_PER_BYTE, "critical bit", critical_names},
    { ft_len, 16/BITS_PER_BYTE, "length", NULL },
    { ft_nat, 16/BITS_PER_BYTE, "transform type", &oakley_group_names },
    { ft_zig, 16/BITS_PER_BYTE, NULL, NULL },
    { ft_end,  0, NULL, NULL }
};

struct_desc ikev2_ke_desc = { "IKEv2 Key Exchange Payload",
			      ikev2ke_fields, sizeof(struct ikev2_ke) };

/*
 * 3.5.  Identification Payloads
 *
 * The Identification Payloads, denoted IDi and IDr in this memo, allow
 * peers to assert an identity to one another.  This identity may be
 * used for policy lookup, but does not necessarily have to match
 * anything in the CERT payload; both fields may be used by an
 * implementation to perform access control decisions.
 *
 * NOTE: In IKEv1, two ID payloads were used in each direction to hold
 * Traffic Selector (TS) information for data passing over the SA.  In
 * IKEv2, this information is carried in TS payloads (see section 3.13).
 *
 * The Identification Payload consists of the IKE generic payload header
 * followed by identification fields as follows:
 *
 *                         1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    ! Next Payload  !C!  RESERVED   !         Payload Length        !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    !   ID Type     !                 RESERVED                      |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    !                                                               !
 *    ~                   Identification Data                         ~
 *    !                                                               !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *             Figure 11:  Identification Payload Format
 */

static field_desc ikev2id_fields[] = {
    { ft_np,  8/BITS_PER_BYTE, "next payload type", &payload_names },
    { ft_set, 8/BITS_PER_BYTE, "critical bit", critical_names},
    { ft_len, 16/BITS_PER_BYTE, "length", NULL },
    { ft_enum, 8/BITS_PER_BYTE, "id_type", &ident_names },
    { ft_mbz,  8/BITS_PER_BYTE, NULL, NULL },
    { ft_mbz, 16/BITS_PER_BYTE, NULL, NULL },
    { ft_end,  0, NULL, NULL }
};

struct_desc ikev2_id_desc = { "IKEv2 Identification Payload",
			      ikev2id_fields, sizeof(struct ikev2_id) };

/* section 3.6
 * The Certificate Payload is defined as follows:
 *
 *                          1                   2                   3
 *      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     ! Next Payload  !C!  RESERVED   !         Payload Length        !
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     ! Cert Encoding !                                               !
 *     +-+-+-+-+-+-+-+-+                                               !
 *     ~                       Certificate Data                        ~
 *     !                                                               !
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 */
static field_desc ikev2_cert_fields[] = {
  { ft_np,  8/BITS_PER_BYTE, "next payload type", &payload_names },
  { ft_set, 8/BITS_PER_BYTE, "critical bit", critical_names},
  { ft_len, 16/BITS_PER_BYTE, "length", NULL },
  { ft_loose_enum,
             8/BITS_PER_BYTE, "ikev2 cert encoding", &ikev2_cert_type_names },
  { ft_end,  0, NULL, NULL }
};

struct_desc ikev2_certificate_desc = { "IKEv2 Certificate Payload", ikev2_cert_fields, IKEV2_CERT_SIZE };

/* section 3.7
 *
 * The Certificate Request Payload is defined as follows:
 *
 *                          1                   2                   3
 *      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     ! Next Payload  !C!  RESERVED   !         Payload Length        !
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     ! Cert Encoding !                                               !
 *     +-+-+-+-+-+-+-+-+                                               !
 *     ~                    Certification Authority                    ~
 *     !                                                               !
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 */

static field_desc ikev2_cert_req_fields[] = {
  { ft_np,  8/BITS_PER_BYTE, "next payload type", &payload_names },
  { ft_set, 8/BITS_PER_BYTE, "critical bit", critical_names},
  { ft_len, 16/BITS_PER_BYTE, "length", NULL },
  { ft_loose_enum,
             8/BITS_PER_BYTE, "ikev2 cert encoding", &ikev2_cert_type_names },
  { ft_end,  0, NULL, NULL }
};

struct_desc ikev2_certificate_req_desc = { "IKEv2 Certificate Request Payload", ikev2_cert_fields, IKEV2_CERT_SIZE };

/*
 * 3.8.  Authentication Payload
 *
 *                         1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    ! Next Payload  !C!  RESERVED   !         Payload Length        !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    ! Auth Method   !                RESERVED                       !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    !                                                               !
 *    ~                      Authentication Data                      ~
 *    !                                                               !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *               Figure 14:  Authentication Payload Format
 *
 */
static field_desc ikev2a_fields[] = {
    { ft_np,  8/BITS_PER_BYTE, "next payload type", &payload_names },
    { ft_set, 8/BITS_PER_BYTE, "critical bit", critical_names},
    { ft_len, 16/BITS_PER_BYTE, "length", NULL },
    { ft_enum, 8/BITS_PER_BYTE, "auth method", &ikev2_auth_names },
    { ft_zig,  8/BITS_PER_BYTE, NULL, NULL },
    { ft_zig, 16/BITS_PER_BYTE, NULL, NULL },
    { ft_end,  0, NULL, NULL }
};

struct_desc ikev2_a_desc = { "IKEv2 Authentication Payload",
			     ikev2a_fields, sizeof(struct ikev2_a) };


/*
 * 3.9.  Nonce Payload
 *
 * The Nonce Payload, denoted Ni and Nr in this memo for the initiator's
 * and responder's nonce respectively, contains random data used to
 * guarantee liveness during an exchange and protect against replay
 * attacks.
 *
 * The Nonce Payload is defined as follows:
 *
 *                         1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    ! Next Payload  !C!  RESERVED   !         Payload Length        !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    !                                                               !
 *    ~                            Nonce Data                         ~
 *    !                                                               !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *                 Figure 15:  Nonce Payload Format
 */
struct_desc ikev2_nonce_desc = { "IKEv2 Nonce Payload",
				 ikev2generic_fields,
				 sizeof(struct ikev2_generic) };


/*    3.10 Notify Payload
 *
 *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    ! Next Payload  !C!  RESERVED   !         Payload Length        !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    !  Protocol ID  !   SPI Size    !      Notify Message Type      !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    !                                                               !
 *    ~                Security Parameter Index (SPI)                 ~
 *    !                                                               !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    !                                                               !
 *    ~                       Notification Data                       ~
 *    !                                                               !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 */
static field_desc ikev2_notify_fields[] = {
  { ft_np, 8/BITS_PER_BYTE, "next payload type", &payload_names },
  { ft_set, 8/BITS_PER_BYTE, "critical bit", critical_names},
  { ft_len, 16/BITS_PER_BYTE, "length", NULL },
  { ft_enum, 8/BITS_PER_BYTE, "Protocol ID", &protocol_names },
  /* names used are v1 names may be we should use 4306 3.3.1 names */
  { ft_nat,  8/BITS_PER_BYTE, "SPI size", NULL},
  { ft_loose_enum, 16/BITS_PER_BYTE, "Notify Message Type", &ikev2_notify_names},
  { ft_end,  0, NULL, NULL }
};

/* IKEv2 Delete Payload
 * layout from RFC 5996 Section 3.11
 * This is followed by a variable length SPI.
 *
 *                      1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * ! Next Payload  !C| RESERVED    !         Payload Length        !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !  Protocol ID  !   SPI Size    !           Num of SPIs         !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * !                                                               !
 * ~               Security Parameter Index(es) (SPI)              ~
 * !                                                               !
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

static field_desc ikev2_delete_fields[] = {
    { ft_np,  8/BITS_PER_BYTE, "next payload type", &payload_names },
    { ft_set, 8/BITS_PER_BYTE, "critical bit", critical_names},
    { ft_len, 16/BITS_PER_BYTE, "length", NULL },
    { ft_nat, 8/BITS_PER_BYTE, "protocol ID", NULL },
    { ft_nat, 8/BITS_PER_BYTE, "SPI size", NULL },
    { ft_nat, 16/BITS_PER_BYTE, "number of SPIs", NULL },
    { ft_end, 0, NULL, NULL }
};

struct_desc ikev2_delete_desc = { "IKEv2 Delete Payload",
                            ikev2_delete_fields, sizeof(struct ikev2_delete) };


struct_desc ikev2_notify_desc = { "IKEv2 Notify Payload",
			     ikev2_notify_fields, sizeof(struct ikev2_notify) };

/*
 * 3.12.  Vendor ID Payload
 *
 *  The Vendor ID Payload fields are defined as follows:
 *
 *                         1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    ! Next Payload  !C!  RESERVED   !         Payload Length        !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    !                                                               !
 *    ~                        Vendor ID (VID)                        ~
 *    !                                                               !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 */
struct_desc ikev2_vendor_id_desc = { "IKEv2 Vendor ID Payload",
				     ikev2generic_fields,
				     sizeof(struct ikev2_generic) };


/*
 * 3.13.  Traffic Selector Payload
 *
 *
 * The Traffic Selector Payload, denoted TS in this memo, allows peers
 * to identify packet flows for processing by IPsec security services.
 * The Traffic Selector Payload consists of the IKE generic payload
 * header followed by individual traffic selectors as follows:
 *
 *                         1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    ! Next Payload  !C!  RESERVED   !         Payload Length        !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    ! Number of TSs !                 RESERVED                      !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    !                                                               !
 *    ~                       <Traffic Selectors>                     ~
 *    !                                                               !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
static field_desc ikev2ts_fields[] = {
    { ft_np,  8/BITS_PER_BYTE, "next payload type", &payload_names },
    { ft_set, 8/BITS_PER_BYTE, "critical bit", critical_names},
    { ft_len, 16/BITS_PER_BYTE, "length", NULL },
    { ft_nat,  8/BITS_PER_BYTE, "number of TS", NULL},
    { ft_zig,  8/BITS_PER_BYTE, NULL, NULL },
    { ft_zig, 16/BITS_PER_BYTE, NULL, NULL },
    { ft_end,  0, NULL, NULL }
};
struct_desc ikev2_ts_desc = { "IKEv2 Traffic Selector Payload",
			     ikev2ts_fields, sizeof(struct ikev2_ts) };


/*
 * 3.13.1.  Traffic Selector
 *
 *                         1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    !   TS Type     !IP Protocol ID*|       Selector Length         |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |           Start Port*         |           End Port*           |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    !                                                               !
 *    ~                         Starting Address*                     ~
 *    !                                                               !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    !                                                               !
 *    ~                         Ending Address*                       ~
 *    !                                                               !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *                Figure 20: Traffic Selector
 */
static field_desc ikev2ts1_fields[] = {
    { ft_enum, 8/BITS_PER_BYTE, "TS type", &ikev2_ts_type_names},
    { ft_nat,  8/BITS_PER_BYTE, "IP Protocol ID", NULL},
    { ft_len, 16/BITS_PER_BYTE, "length", NULL },
    { ft_nat, 16/BITS_PER_BYTE, "start port", NULL},
    { ft_nat, 16/BITS_PER_BYTE, "end port", NULL},
    { ft_end,  0, NULL, NULL }
};
struct_desc ikev2_ts1_desc = { "IKEv2 Traffic Selector",
			       ikev2ts1_fields, sizeof(struct ikev2_ts1) };


/*
 * 3.14.  Encrypted Payload
 *
 *                         1                   2                   3
 *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    ! Next Payload  !C!  RESERVED   !         Payload Length        !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    !                     Initialization Vector                     !
 *    !         (length is block size for encryption algorithm)       !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    ~                    Encrypted IKE Payloads                     ~
 *    +               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    !               !             Padding (0-255 octets)            !
 *    +-+-+-+-+-+-+-+-+                               +-+-+-+-+-+-+-+-+
 *    !                                               !  Pad Length   !
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    ~                    Integrity Checksum Data                    ~
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *             Figure 21:  Encrypted Payload Format
 *
 * Unlike other payloads, the encrypted payload is a container, so the
 * next payload is set to the inner type. No next payload is allowed
 * otherwise.
 */
static field_desc ikev2e_fields[] = {
    { ft_np_in,8/BITS_PER_BYTE, "next payload type", &payload_names },
    { ft_set,  8/BITS_PER_BYTE, "critical bit", critical_names},
    { ft_len, 16/BITS_PER_BYTE, "length", NULL },
    { ft_end,  0, NULL, NULL }
};

struct_desc ikev2_e_desc = { "IKEv2 Encryption Payload",
			      ikev2e_fields,
			     sizeof(struct ikev2_generic)};



/* descriptor for each payload type
 *
 * There is a slight problem in that some payloads differ, depending
 * on the mode.  Since this is table only used for top-level payloads,
 * Proposal and Transform payloads need not be handled.
 * That leaves only Identification payloads as a problem.
 * We make all these entries NULL
 */
static struct_desc *const payload_descs[] = {
    NULL,				/* 0 ISAKMP_NEXT_NONE (No other payload following) */
    &isakmp_sa_desc,			/* 1 ISAKMP_NEXT_SA (Security Association) */
    NULL,				/* 2 ISAKMP_NEXT_P (Proposal) */
    NULL,				/* 3 ISAKMP_NEXT_T (Transform) */
    &isakmp_keyex_desc,			/* 4 ISAKMP_NEXT_KE (Key Exchange) */
    NULL,				/* 5 ISAKMP_NEXT_ID (Identification) */
    &isakmp_ipsec_certificate_desc,	/* 6 ISAKMP_NEXT_CERT (Certificate) */
    &isakmp_ipsec_cert_req_desc,	/* 7 ISAKMP_NEXT_CR (Certificate Request) */
    &isakmp_hash_desc,			/* 8 ISAKMP_NEXT_HASH (Hash) */
    &isakmp_signature_desc,		/* 9 ISAKMP_NEXT_SIG (Signature) */
    &isakmp_nonce_desc,			/* 10 ISAKMP_NEXT_NONCE (Nonce) */
    &isakmp_notification_desc,		/* 11 ISAKMP_NEXT_N (Notification) */
    &isakmp_delete_desc,		/* 12 ISAKMP_NEXT_D (Delete) */
    &isakmp_vendor_id_desc,		/* 13 ISAKMP_NEXT_VID (Vendor ID) */
    &isakmp_attr_desc,                  /* 14 ISAKMP_NEXT_ATTR (ModeCfg)  */
    NULL,                               /* 15 */
    NULL,                               /* 16 */
    NULL,                               /* 17 */
    NULL,                               /* 18 */
    NULL,                               /* 19 */
    &isakmp_nat_d,                      /* 20=130 ISAKMP_NEXT_NATD (NAT-D) */
    &isakmp_nat_oa,                     /* 21=131 ISAKMP_NEXT_NATOA (NAT-OA) */
    NULL, NULL, NULL, NULL,             /* 22,23,24,25 */
    NULL, NULL, NULL, NULL,             /* 26,27,28,29 */
    NULL, NULL, NULL,                   /* 30,31,32 */
    &ikev2_sa_desc,                     /* 33 ISAKMP_NEXT_v2SA */
    &ikev2_ke_desc,                     /* 34 ISAKMP_NEXT_v2KE */
    &ikev2_id_desc,                     /* 35 ISAKMP_NEXT_v2IDi */
    &ikev2_id_desc,                     /* 36 ISAKMP_NEXT_v2IDr */
    &ikev2_certificate_desc,            /* 37 ISAKMP_NEXT_v2CERT */
    &ikev2_certificate_req_desc,	/* 38 ISAKMP_NEXT_v2CERTREQ */
    &ikev2_a_desc,                      /* 39 ISAKMP_NEXT_v2AUTH */
    &ikev2_nonce_desc,                  /* 40 ISAKMP_NEXT_v2Ni/ISAKMP_NEXT_v2Nr */
    &ikev2_notify_desc,                 /* 41 ISAKMP_NEXT_v2N */
    &ikev2_delete_desc,                 /* 42 ISAKMP_NEXT_v2D */
    &ikev2_vendor_id_desc,              /* 43 ISAKMP_NEXT_v2V */
    &ikev2_ts_desc,                     /* 44 ISAKMP_NEXT_v2TSi */
    &ikev2_ts_desc,                     /* 45 ISAKMP_NEXT_v2TSr */
    &ikev2_e_desc,                      /* 46 ISAKMP_NEXT_v2E */
};
/*根据下一载荷值，获取对应的描述信息*/
const struct_desc *payload_desc(unsigned p)
{
	return p < elemsof(payload_descs) ? payload_descs[p] : NULL;
}

void
init_pbs(pb_stream *pbs, u_int8_t *start, size_t len, const char *name)
{
/*pbs内容全部清空*/
    pbs->container = NULL;
    pbs->desc = NULL;
    pbs->name = name;
    pbs->start = pbs->cur = start;
    pbs->roof = start + len;
    pbs->lenfld = NULL;
    pbs->lenfld_desc = NULL;
    pbs->next_payload_pointer = NULL;
}

void
init_sub_pbs(pb_stream *parent_pbs, pb_stream *child_pbs, const char *name)
{
	/*child_pbs初始化完成后再更新为parent_pbs中的成员值*/
    init_pbs(child_pbs, parent_pbs->cur
             , parent_pbs->roof - parent_pbs->cur, name);

    child_pbs->container = parent_pbs;
    child_pbs->desc = NULL;
    child_pbs->cur = parent_pbs->cur;
    child_pbs->next_payload_pointer = parent_pbs->next_payload_pointer;
}

#ifdef DEBUG

/* print a host struct
 *
 * This code assumes that the network and host structure
 * members have the same alignment and size!  This requires
 * that all padding be explicit.
 */
void
DBG_print_struct(const char *label, const void *struct_ptr
, struct_desc *sd, bool len_meaningful)
{
    bool immediate = FALSE;
    const u_int8_t *inp = struct_ptr;
    field_desc *fp;

    DBG_log("%s%s:", label, sd->name);

    for (fp = sd->fields; fp->field_type != ft_end; fp++)
    {
	int i = fp->size;
	u_int32_t n = 0;

	switch (fp->field_type)
	{
	case ft_np:	/* these are next-payload values, and are often zero, because they */
	case ft_np_in:	/* updated when the next payload is inserted. Omit as confusing */
            inp += 1;
            break;
	case ft_mbz:	/* must be zero */
	case ft_zig:
	    inp += i;
	    break;
	case ft_nat:	/* natural number (may be 0) */
	case ft_len:	/* length of this struct and any following crud */
	case ft_lv:	/* length/value field of attribute */
	case ft_enum:	/* value from an enumeration */
	case ft_loose_enum:	/* value from an enumeration with only some names known */
	case ft_af_enum:	/* Attribute Format + value from an enumeration */
	case ft_af_loose_enum:	/* Attribute Format + value from an enumeration */
	case ft_set:	/* bits representing set */
	    switch (i)
	    {
	    case 8/BITS_PER_BYTE:
		n = *(const u_int8_t *)inp;
		break;
	    case 16/BITS_PER_BYTE:
		n = *(const u_int16_t *)inp;
		break;
	    case 32/BITS_PER_BYTE:
		n = *(const u_int32_t *)inp;
		break;
	    default:
		bad_case(i);
	    }
	    switch (fp->field_type)
	    {
	    case ft_len:	/* length of this struct and any following crud */
	    case ft_lv:		/* length/value field of attribute */
		if (!immediate && !len_meaningful)
		    break;
		/* FALL THROUGH */
	    case ft_nat:	/* natural number (may be 0) */
		DBG_log("   %s: %lu", fp->name, (unsigned long)n);
		break;

	    case ft_np:	        /* value from an enumeration with next payload*/
	    case ft_np_in:      /* value from an enumeration with next payload*/
	    case ft_af_loose_enum: /* Attribute Format + value from an enumeration */
	    case ft_af_enum:	/* Attribute Format + value from an enumeration */
		if ((n & ISAKMP_ATTR_AF_MASK) == ISAKMP_ATTR_AF_TV)
		    immediate = TRUE;
		/* FALL THROUGH */
	    case ft_enum:	/* value from an enumeration */
	    case ft_loose_enum:	/* value from an enumeration with only some names known */
		DBG_log("   %s: %s", fp->name, enum_show(fp->desc, n));
		break;
	    case ft_set:	/* bits representing set */
		DBG_log("   %s: %s", fp->name, bitnamesof(fp->desc, n));
		break;
	    default:
		bad_case(fp->field_type);
	    }
	    inp += i;
	    break;

	case ft_raw:	/* bytes to be left in network-order */
	    {
		char m[50];	/* arbitrary limit on name width in log */

		snprintf(m, sizeof(m), "   %s:", fp->name);
		DBG_dump(m, inp, i);
		inp += i;
	    }
	    break;
	default:
	    bad_case(fp->field_type);
	}
    }
}

static void
DBG_prefix_print_struct(const pb_stream *pbs
			, const char *label, const void *struct_ptr
			, struct_desc *sd, bool len_meaningful)
{
    /* print out a title, with a prefix of asterisks to show
     * the nesting level.
     */
    char space[40];	/* arbitrary limit on label+flock-of-* */
    size_t len = strlen(label);

    if (sizeof(space) <= len)
    {
	DBG_print_struct(label, struct_ptr, sd, len_meaningful);
    }
    else
    {
	const pb_stream *p = pbs;
	char *pre = &space[sizeof(space) - (len + 1)];

	strcpy(pre, label);

	/* put at least one * out */
	for (;;)
	{
	    if (pre <= space)
		break;
	    *--pre = '*';
	    if (p == NULL)
		break;
	    p = p->container;
	}
	DBG_print_struct(pre, struct_ptr, sd, len_meaningful);
    }
}

#endif

/* "parse" a network struct into a host struct.
 *
 * This code assumes that the network and host structure
 * members have the same alignment and size!  This requires
 * that all padding be explicit.
 *
 * If obj_pbs is supplied, a new pb_stream is created for the
 * variable part of the structure (this depends on their
 * being one length field in the structure).  The cursor of this
 * new PBS is set to after the parsed part of the struct.
 *
 * This routine returns TRUE iff it succeeds.
 */

/****************************************************
* struct_ptr:  解析后的输出数据
* sd        :  描述信息			
* ins       :  输入的网络字节序数据流
* obj_pbs   :  指向该结构后面尚未解析的ins数据部分
*****************************************************/

bool
in_struct(void *struct_ptr, struct_desc *sd
, pb_stream *ins, pb_stream *obj_pbs)
{
    err_t ugh = NULL;
    u_int8_t *cur = ins->cur;/*待解析数据开始*/
	/*确保有待解析的数据大小足够，否则无法成功解析为sd描述的大小*/
    if (ins->roof - cur < (ptrdiff_t)sd->size)
    {
        ugh = builddiag("not enough room in input packet for %s"
                        " (remain=%li, sd->size=%zu)"
                        , sd->name, (long int)(ins->roof - cur), sd->size);

    }
    else
    {
    	/*root指向待解析数据的末尾*/
	u_int8_t *roof = cur + sd->size;    /* may be changed by a length field *//*未考虑变长属性*/
	u_int8_t *outp = struct_ptr;/*输出结构体*/
	bool immediate = FALSE;
	field_desc *fp;

	for (fp = sd->fields; ugh == NULL; fp++)
	{
	    size_t i = fp->size;
	    /*passert条件成立，继续执行;不成立，直接退出*/
	    passert(ins->roof - cur >= (ptrdiff_t)i);
	    passert(cur - ins->cur <= (ptrdiff_t)(sd->size - i));
	    passert(outp - (cur - ins->cur) == struct_ptr);/*解析长度相同*/

#if 0
	    DBG(DBG_PARSING, DBG_log("%d %s"
		, (int) (cur - ins->cur), fp->name == NULL? "" : fp->name));
#endif
	    switch (fp->field_type)
	    {
	    case ft_mbz:	/* must be zero *//*全零域，*/
		for (; i != 0; i--)
		{
		    if (*cur++ != 0)
		    {
			ugh = builddiag("byte %d of %s must be zero, but is not"
			    , (int) (cur - ins->cur), sd->name);
			break;
		    }
		    *outp++ = '\0';	/* probably redundant */
		}
		break;
	    case ft_zig:	/* should be zero, ignore if not *//*全零域，如果不是可以忽略其数值*/
		for (; i != 0; i--)
		{
		    if (*cur++ != 0)
		    {
			openswan_log("byte %d of %s should have been zero, but was not"
			    , (int) (cur - ins->cur), sd->name);
			/*
			 * We cannot zeroize it, it would break our hash calculation
			 * *cur = '\0';
			 */
		    }
		    *outp++ = '\0';	/* probably redundant */
		}
		break;

	    case ft_nat:	/* natural number (may be 0) */
	    case ft_len:	/* length of this struct and any following crud */
	    case ft_lv:		/* length/value field of attribute */
	    case ft_enum:	/* value from an enumeration */
	    case ft_np:	        /* value from an enumeration */
	    case ft_np_in:      /* value from an enumeration */
	    case ft_loose_enum:	/* value from an enumeration with only some names known */
	    case ft_af_enum:	/* Attribute Format + value from an enumeration */
	    case ft_af_loose_enum:	/* Attribute Format + value from an enumeration */
	    case ft_set:	/* bits representing set */
		    {
			u_int32_t n = 0;

			/* Reportedly fails on arm, see bug #775 */
			for (; i != 0; i--)
			    n = (n << BITS_PER_BYTE) | *cur++;/*根据该属性的长度来获取值: 单字节、双字节、四字节*/
											   /*此处已经实现了字节序的转换(ntohl, ntohs)*/

			switch (fp->field_type)/*ikev2_trans_attr_fields*/
			{
				case ft_len:	/* length of this struct and any following crud *//*通用载荷头部*/
				case ft_lv:	/* length/value field of attribute *//*属性载荷头部*/
				{/*载荷长度字段，此处是长度可变载荷，无法预定义长度，因此需要更新长度信息*/
				    u_int32_t len = fp->field_type == ft_len? n
					: immediate? sd->size : n + sd->size;

				    if (len < sd->size)
				    {
					ugh = builddiag("%s of %s is smaller than minimum"
					    , fp->name, sd->name);
				    }
				    else if (pbs_left(ins) < len)
				    {
					ugh = builddiag("%s of %s is larger than can fit"
					    , fp->name, sd->name);
				    }
				    else
				    {/*当前载荷的roof*/
					roof = ins->cur + len;/*根据报文中实际的长度定位数据末尾*/
				    }
				    break;
				}
				case ft_af_loose_enum:	/* Attribute Format + value from an enumeration */
				    if ((n & ISAKMP_ATTR_AF_MASK) == ISAKMP_ATTR_AF_TV)/*通过最高位判断该载荷类型，0:变长，1:定长*/
					immediate = TRUE;
				    break;

				case ft_af_enum:	/* Attribute Format + value from an enumeration */
				    if ((n & ISAKMP_ATTR_AF_MASK) == ISAKMP_ATTR_AF_TV)
					immediate = TRUE;
				    /* FALL THROUGH */
				case ft_enum:	/* value from an enumeration */
				    if (enum_name(fp->desc, n) == NULL)/*判断报文中枚举类型的值是否合法*/
				    {
					ugh = builddiag("%s of %s has an unknown value: %lu"
					    , fp->name, sd->name, (unsigned long)n);
				    }
				    /* FALL THROUGH */
				case ft_loose_enum:	/* value from an enumeration with only some names known */
				    break;
				case ft_set:	/* bits representing set */
				    if (!testset(fp->desc, n))/*判断报文中某些bit标志位是否合法*/
				    {
					ugh = builddiag("bitset %s of %s has unknown member(s): %s"
					    , fp->name, sd->name, bitnamesof(fp->desc, n));
				    }
				    break;
				default:
					break;
			}
			
			i = fp->size;
			switch (i)				/*将转后字节序后的报文中的数据存储到输出结构struct_ptr中*/
			{
			case 8/BITS_PER_BYTE:
			    *(u_int8_t *)outp = n;
			    break;
			case 16/BITS_PER_BYTE:
			    *(u_int16_t *)outp = n;
			    break;
			case 32/BITS_PER_BYTE:
			    *(u_int32_t *)outp = n;
			    break;
			default:
			    bad_case(i);
			}
			outp += i;
			break;
		    }

	    case ft_raw:	/* bytes to be left in network-order *//*原始类型报文直接赋值即可，不用区分大小端、不同判断数据是否合法*/
		for (; i != 0; i--)
		{
		    *outp++ = *cur++;
		}
		break;

	    case ft_end:	/* end of field list *//*解析到结构体的末尾*/
		passert(cur == ins->cur + sd->size);/*检查解析的长度是否正确*/
		if (obj_pbs != NULL)/*如果传入该参数*/
		{
		    /******************************************************************************
		    *通过init_pbs()将obj_pbs指向ins数据的开始和结束位置，
		    *然后更新obj_pbs中cur指针为已经解析的位置(确切的说是已经成功解析部分的下一个字节)
		    *但是如果有变长部分，则cur指向的为变长部分第一个字节；它没有拷贝到传入的struct_ptr中
		    ******************************************************************************/
		    init_pbs(obj_pbs, ins->cur, roof - ins->cur, sd->name);
		    obj_pbs->container = ins;
		    obj_pbs->desc = sd;
		    obj_pbs->cur = cur;
		}
		ins->cur = roof;/*更新cur指针到数据的sd结构之后，如果不存在变长载荷应该与cur指的位置相同；存在的话不相同*/
		/*注意: 变长载荷数据部分没有继续解析且不能通过ins来获取，但是可以通过obj_pbs来获取变长载荷的数据部分*/
		DBG(DBG_PARSING
		    , DBG_prefix_print_struct(ins, "parse ", struct_ptr, sd, TRUE));
		return TRUE;

	    default:
		bad_case(fp->field_type);
	    }
	}
    }

    /* some failure got us here: report it */
    openswan_loglog(RC_LOG_SERIOUS, "%s", ugh);
    return FALSE;
}

bool
in_raw(void *bytes, size_t len, pb_stream *ins, const char *name)
{
    if (pbs_left(ins) < len)
    {
	openswan_loglog(RC_LOG_SERIOUS
			, "not enough bytes left to get %s from %s"
			, name, ins->name);
	return FALSE;
    }
    else
    {
	if (bytes == NULL)
	{
	    DBG(DBG_PARSING
		, DBG_log("skipping %u raw bytes of %s (%s)"
		    , (unsigned) len, ins->name, name);
		  DBG_dump(name, ins->cur, len));
	}
	else
	{
	    memcpy(bytes, ins->cur, len);
	    DBG(DBG_PARSING
		, DBG_log("parsing %u raw bytes of %s into %s"
		    , (unsigned) len, ins->name, name);
		  DBG_dump(name, bytes, len));
	}
	ins->cur += len;
	return TRUE;
    }
}

/* "emit" a host struct into a network packet.
 *
 * This code assumes that the network and host structure
 * members have the same alignment and size!  This requires
 * that all padding be explicit.
 *
 * If obj_pbs is non-NULL, its pbs describes a new output stream set up
 * to contain the object.  The cursor will be left at the variable part.
 * This new stream must subsequently be finalized by close_output_pbs().
 *
 * The value of any field of type ft_len is computed, not taken
 * from the input struct.  The length is actually filled in when
 * the object's output stream is finalized.  If obj_pbs is NULL,
 * finalization is done by out_struct before it returns.
 *
 * This routine returns TRUE iff it succeeds.
 */
/****************************************************************

将struct_ptr按照sd的描述方式拷贝到outs中。同时如果obj_pbs存在，
则使obj_pbs指向outs数据部分，并更新obj_pbs的cur指针到新填充的位置，
然后将outs的cur设置到最大，其他函数不得再操作outs,除非使用close_output_pbs更新才行
****************************************************************/
bool
out_struct(const void *struct_ptr, struct_desc *sd
	   , pb_stream *outs, pb_stream *obj_pbs)
{
    err_t ugh = NULL;
    const u_int8_t *inp = struct_ptr;
    u_int8_t *cur = outs->cur;

    DBG(DBG_EMITTING
	, DBG_prefix_print_struct(outs, "emit ", struct_ptr, sd, obj_pbs==NULL));

    if (outs->roof - cur < (ptrdiff_t)sd->size)
    {
	ugh = builddiag("not enough room left in output packet to place %s"
	    , sd->name);
    }
    else
    {
	bool immediate = FALSE;
	pb_stream obj;
	field_desc *fp;

	obj.lenfld = NULL;  /* until a length field is discovered */
	obj.lenfld_desc = NULL;

	for (fp = sd->fields; ugh == NULL; fp++)
	{
	    size_t i = fp->size;

	    /* make sure that there is space for the next structure element */
	    passert(outs->roof - cur >= (ptrdiff_t)i);

	    /* verify that the spot is correct in the offset */
	    passert(cur - outs->cur <= (ptrdiff_t)(sd->size - i));

	    /* verify that we are at the right place in the input structure */
	    passert(inp - (cur - outs->cur) == struct_ptr);

#if 0
	    DBG_log("out_struct: %d %s"
		    , (int) (cur - outs->cur), fp->name == NULL? "<end>" : fp->name);
#endif
            if(fp->field_type == ft_np) {
                outs->next_payload_pointer = cur;
            }
            else if(fp->field_type == ft_np_in) {
                obj.next_payload_pointer = cur;
            }

	    switch (fp->field_type)
	    {
	    case ft_mbz:	/* must be zero */
	    case ft_zig:	/* should be zero, but we'll let it go */
		inp += i;
		for (; i != 0; i--)
		    *cur++ = '\0';
		break;
	    case ft_np:	        /* value from an enumeration, note location */
	    case ft_np_in:      /* value from an enumeration, note location */
	    case ft_nat:	/* natural number (may be 0) */
	    case ft_len:	/* length of this struct and any following crud */
	    case ft_lv:		/* length/value field of attribute */
	    case ft_enum:	/* value from an enumeration */
	    case ft_loose_enum:	/* value from an enumeration with only some names known */
	    case ft_af_enum:	/* Attribute Format + value from an enumeration */
	    case ft_af_loose_enum: /* Attribute Format + value from an enumeration */
	    case ft_set:	/* bits representing set */

	    {
		u_int32_t n = 0;

		switch (i)
		{
		case 8/BITS_PER_BYTE:
		    n = *(const u_int8_t *)inp;
		    break;
		case 16/BITS_PER_BYTE:
		    n = *(const u_int16_t *)inp;
		    break;
		case 32/BITS_PER_BYTE:
		    n = *(const u_int32_t *)inp;
		    break;
		default:
		    bad_case(i);
		}

		switch (fp->field_type)
		{
		case ft_len:	/* length of this struct and any following crud */
		case ft_lv:	/* length/value field of attribute */
		    if (immediate)
			break;	/* not a length */
		    /* We can't check the length because it will likely
		     * be filled in after variable part is supplied.
		     * We do record where this is so that it can be
		     * filled in by a subsequent close_output_pbs().
		     */
		    passert(obj.lenfld == NULL);	/* only one ft_len allowed */
		    obj.lenfld = cur;
		    obj.lenfld_desc = fp;
		    break;
		case ft_af_loose_enum: /* Attribute Format + value from an enumeration */
		    if ((n & ISAKMP_ATTR_AF_MASK) == ISAKMP_ATTR_AF_TV)
			immediate = TRUE;
		    break;

		case ft_af_enum:	/* Attribute Format + value from an enumeration */
		    if ((n & ISAKMP_ATTR_AF_MASK) == ISAKMP_ATTR_AF_TV)
			immediate = TRUE;
		    /* FALL THROUGH */
		case ft_enum:	/* value from an enumeration */
		    if (enum_name(fp->desc, n) == NULL)
		    {
			ugh = builddiag("%s of %s has an unknown value: %lu"
			    , fp->name, sd->name, (unsigned long)n);
		    }
		    /* FALL THROUGH */
		case ft_loose_enum:	/* value from an enumeration with only some names known */
		    break;
		case ft_set:	/* bits representing set */
		    if (!testset(fp->desc, n))
		    {
			ugh = builddiag("bitset %s of %s has unknown member(s): %s"
			    , fp->name, sd->name, bitnamesof(fp->desc, n));
		    }
		    break;
		default:
		    break;
		}

		while (i-- != 0)
		{
		    cur[i] = (u_int8_t)n;
		    n >>= BITS_PER_BYTE;
		}
		inp += fp->size;
		cur += fp->size;
		break;
	    }
	    case ft_raw:	/* bytes to be left in network-order */
		for (; i != 0; i--)
		    *cur++ = *inp++;
		break;
	    case ft_end:	/* end of field list */
		passert(cur == outs->cur + sd->size);

		obj.container = outs;
		obj.desc = sd;
		obj.name = sd->name;
		obj.start = outs->cur;
		obj.cur = cur;
		obj.roof = outs->roof;	/* limit of possible */
		/* obj.lenfld and obj.lenfld_desc already set */

		if (obj_pbs == NULL)
		{
		    close_output_pbs(&obj); /* fill in length field, if any */
		}
		else
		{
		    /* We set outs->cur to outs->roof so that
		     * any attempt to output something into outs
		     * before obj is closed will trigger an error.
		     */
		    outs->cur = outs->roof;

		    *obj_pbs = obj;
		}
		return TRUE;

	    default:
		bad_case(fp->field_type);
	    }
	}
    }

    /* some failure got us here: report it */
    loglog(RC_LOG_SERIOUS, "%s", ugh);	/* ??? serious, but errno not relevant */
    return FALSE;
}

/* Find last complete top-level payload and change its np
 *  * Note: we must deal with payloads already formatted for the network.
 *  _*_Note:_we_don't_think_a_FALSE_return_should_happen_but_old_routine_did.
 *   */
/* XXX replace all instances of this */
bool
out_modify_previous_np(u_int8_t np, pb_stream *outs)
{
    u_int8_t *pl = outs->start;
    size_t left = outs->cur - outs->start;

    passert(left >= NSIZEOF_isakmp_hdr);    /* not even room for isakmp_hdr! */
    if (left == NSIZEOF_isakmp_hdr) {
	/* no payloads, just the isakmp_hdr: insert np here */
	passert(pl[NOFFSETOF_isa_np] == ISAKMP_NEXT_NONE ||
		pl[NOFFSETOF_isa_np] == ISAKMP_NEXT_HASH);
	pl[NOFFSETOF_isa_np] = np;
    } else {
	pl += NSIZEOF_isakmp_hdr;       /* skip over isakmp_hdr */
	left -= NSIZEOF_isakmp_hdr;
	for (;;) {
		size_t pllen;

		passert(left >= NSIZEOF_isakmp_generic);
		pllen = (pl[NOFFSETOF_isag_length] << 8)/*payload 一般为两个字节*/
			| pl[NOFFSETOF_isag_length + 1];
		passert(left >= pllen);
		if (left == pllen) {/*当前载荷长度和剩余长度相同时，说明已经找到上一个载荷*/
			/* found last top-level payload */
			pl[NOFFSETOF_isag_np] = np;
			break;  /* done */
		} else {
			/* this payload is not the last: scan forward */
			pl += pllen;
			left -= pllen;
		}
	}
	}
	return TRUE;
}

void pbs_set_np(pb_stream *outs, u_int8_t np)
{
    passert(outs->next_payload_pointer != NULL);
    DBG(DBG_EMITTING, DBG_log("   next-payload: %s [@%ld=0x%2x]"
                              , enum_show(&payload_names, np)
                              , (long)(outs->next_payload_pointer - outs->start)
                              , np));

    *outs->next_payload_pointer = np;/*可以这样做的原因在于np字段设置为ft_np_in，next_payload_pointer指向了此位置*/
}
/*载荷通用部分*/
bool
out_generic(u_int8_t np, struct_desc *sd
, pb_stream *outs, pb_stream *obj_pbs)
{
    struct isakmp_generic gen;

    passert(sd->fields == isakmp_generic_desc.fields);
    gen.isag_np = np;
    return out_struct(&gen, sd, outs, obj_pbs);
}

bool
out_generic_raw(u_int8_t np, struct_desc *sd
, pb_stream *outs, const void *bytes, size_t len, const char *name)
{
    pb_stream pbs;

    if (!out_generic(np, sd, outs, &pbs)
    || !out_raw(bytes, len, &pbs, name))
	return FALSE;
    close_output_pbs(&pbs);
    return TRUE;
}

bool
out_raw(const void *bytes, size_t len, pb_stream *outs, const char *name)
{
    if (pbs_left(outs) < len)
    {
	loglog(RC_LOG_SERIOUS, "not enough room left to place %lu bytes of %s in %s"
	    , (unsigned long) len, name, outs->name);
	return FALSE;
    }
    else
    {
	DBG(DBG_EMITTING
	    , DBG_log("emitting %u raw bytes of %s into %s"
		, (unsigned) len, name, outs->name);
	      DBG_dump(name, bytes, len));
	memcpy(outs->cur, bytes, len);
	outs->cur += len;
	return TRUE;
    }
}

bool
out_zero(size_t len, pb_stream *outs, const char *name)
{
    if (pbs_left(outs) < len)
    {
	loglog(RC_LOG_SERIOUS, "not enough room left to place %s in %s", name, outs->name);
	return FALSE;
    }
    else
    {
	DBG(DBG_EMITTING, DBG_log("emitting %u zero bytes of %s into %s"
	    , (unsigned) len, name, outs->name));
	memset(outs->cur, 0x00, len);
	outs->cur += len;
	return TRUE;
    }
}

/* Record current length.
 * Note: currently, this may be repeated any number of times;
 * the last one wins.
 */
void
close_output_pbs(pb_stream *pbs)
{
    if (pbs->lenfld != NULL)
    {
	u_int32_t len = pbs_offset(pbs);
	int i = pbs->lenfld_desc->size;

	if (pbs->lenfld_desc->field_type == ft_lv)
	    len -= sizeof(struct isakmp_attribute);
	DBG(DBG_EMITTING, DBG_log("emitting length of %s: %lu"
	    , pbs->name, (unsigned long) len));
	while (i-- != 0)
	{
	    pbs->lenfld[i] = (u_int8_t)len;
	    len >>= BITS_PER_BYTE;
	}
    }
    if (pbs->container != NULL)
	pbs->container->cur = pbs->cur;	/* pass space utilization up */
}

/*
 * Local Variables:
 * c-basic-offset:4
 * c-style: pluto
 * End:
 */
