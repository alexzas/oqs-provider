// SPDX-License-Identifier: Apache-2.0 AND MIT

/*
 * OQS OpenSSL 3 provider
 *
 * Code strongly inspired by OpenSSL common provider capabilities.
 *
 * ToDo: Interop testing.
 */

#include <assert.h>
#include <string.h>
#include <openssl/core_dispatch.h>
#include <openssl/core_names.h>

/* For TLS1_VERSION etc */
#include <openssl/ssl.h>
#include <openssl/params.h>

// internal, but useful OSSL define:
# define OSSL_NELEM(x)    (sizeof(x)/sizeof((x)[0]))

#include "oqs_prov.h"

typedef struct oqs_group_constants_st {
    unsigned int group_id;           /* Group ID */
    unsigned int group_id_ecp_hyb;   /* Group ID of hybrid with ECP */
    unsigned int group_id_ecx_hyb;   /* Group ID of hybrid with ECX */
    unsigned int secbits;            /* Bits of security */
    int mintls;                      /* Minimum TLS version, -1 unsupported */
    int maxtls;                      /* Maximum TLS version (or 0 for undefined) */
    int mindtls;                     /* Minimum DTLS version, -1 unsupported */
    int maxdtls;                     /* Maximum DTLS version (or 0 for undefined) */
    int is_kem;                      /* Always set */
} OQS_GROUP_CONSTANTS;

static OQS_GROUP_CONSTANTS oqs_group_list[] = {
    // ad-hoc assignments - take from OQS generate data structures
///// OQS_TEMPLATE_FRAGMENT_GROUP_ASSIGNMENTS_START
   { 0x0200, 0x2F00, 0x2F80, 128, TLS1_3_VERSION, 0, -1, -1, 1 },
   { 0x0201, 0x2F01, 0x2F81, 128, TLS1_3_VERSION, 0, -1, -1, 1 },
   { 0x0202, 0x2F02, 0x2F82, 192, TLS1_3_VERSION, 0, -1, -1, 1 },
   { 0x0203, 0x2F03, 0x2F83, 192, TLS1_3_VERSION, 0, -1, -1, 1 },
   { 0x0204, 0x2F04, 0     , 256, TLS1_3_VERSION, 0, -1, -1, 1 },
   { 0x0205, 0x2F05, 0     , 256, TLS1_3_VERSION, 0, -1, -1, 1 },
   { 0x023A, 0x2F3A, 0x2F39, 128, TLS1_3_VERSION, 0, -1, -1, 1 },
   { 0x023C, 0x2F3C, 0x2F90, 192, TLS1_3_VERSION, 0, -1, -1, 1 },
   { 0x023D, 0x2F3D, 0     , 256, TLS1_3_VERSION, 0, -1, -1, 1 },
   { 0x0241, 0x2F41, 0x2FAE, 128, TLS1_3_VERSION, 0, -1, -1, 1 },
   { 0x0242, 0x2F42, 0x2FAF, 192, TLS1_3_VERSION, 0, -1, -1, 1 },
   { 0x0243, 0x2F43, 0     , 256, TLS1_3_VERSION, 0, -1, -1, 1 },
   { 0x023E, 0x2F3E, 0x2FA9, 128, TLS1_3_VERSION, 0, -1, -1, 1 },
   { 0x023F, 0x2F3F, 0x2FAA, 192, TLS1_3_VERSION, 0, -1, -1, 1 },
   { 0x0240, 0x2F40, 0     , 256, TLS1_3_VERSION, 0, -1, -1, 1 },
   { 0x022C, 0x2F2C, 0x2FAC, 128, TLS1_3_VERSION, 0, -1, -1, 1 },
   { 0x022D, 0x2F2D, 0x2FAD, 192, TLS1_3_VERSION, 0, -1, -1, 1 },
   { 0x022E, 0x2F2E, 0     , 256, TLS1_3_VERSION, 0, -1, -1, 1 },
///// OQS_TEMPLATE_FRAGMENT_GROUP_ASSIGNMENTS_END
};

// Adds entries for tlsname, `ecx`_tlsname and `ecp`_tlsname
#define OQS_GROUP_ENTRY(tlsname, realname, algorithm, sb, idx) \
    { \
        OSSL_PARAM_utf8_string(OSSL_CAPABILITY_TLS_GROUP_NAME, \
                               #tlsname, \
                               sizeof(#tlsname)), \
        OSSL_PARAM_utf8_string(OSSL_CAPABILITY_TLS_GROUP_NAME_INTERNAL, \
                               #realname, \
                               sizeof(#realname)), \
        OSSL_PARAM_utf8_string(OSSL_CAPABILITY_TLS_GROUP_ALG, \
                               #algorithm, \
                               sizeof(#algorithm)), \
        OSSL_PARAM_uint(OSSL_CAPABILITY_TLS_GROUP_ID, \
                        (unsigned int *)&oqs_group_list[idx].group_id), \
        OSSL_PARAM_uint(OSSL_CAPABILITY_TLS_GROUP_SECURITY_BITS, \
                        (unsigned int *)&oqs_group_list[idx].secbits), \
        OSSL_PARAM_int(OSSL_CAPABILITY_TLS_GROUP_MIN_TLS, \
                        (unsigned int *)&oqs_group_list[idx].mintls), \
        OSSL_PARAM_int(OSSL_CAPABILITY_TLS_GROUP_MAX_TLS, \
                        (unsigned int *)&oqs_group_list[idx].maxtls), \
        OSSL_PARAM_int(OSSL_CAPABILITY_TLS_GROUP_MIN_DTLS, \
                        (unsigned int *)&oqs_group_list[idx].mindtls), \
        OSSL_PARAM_int(OSSL_CAPABILITY_TLS_GROUP_MAX_DTLS, \
                        (unsigned int *)&oqs_group_list[idx].maxdtls), \
        OSSL_PARAM_int(OSSL_CAPABILITY_TLS_GROUP_IS_KEM, \
                        (unsigned int *)&oqs_group_list[idx].is_kem), \
        OSSL_PARAM_END \
    }

#define OQS_GROUP_ENTRY_ECP(tlsname, realname, algorithm, sb, idx) \
    { \
        OSSL_PARAM_utf8_string(OSSL_CAPABILITY_TLS_GROUP_NAME, \
                               ECP_NAME(sb, tlsname), \
                               sizeof(ECP_NAME(sb, tlsname))), \
        OSSL_PARAM_utf8_string(OSSL_CAPABILITY_TLS_GROUP_NAME_INTERNAL, \
                               ECP_NAME(sb, realname), \
                               sizeof(ECP_NAME(sb, realname))), \
        OSSL_PARAM_utf8_string(OSSL_CAPABILITY_TLS_GROUP_ALG, \
                               ECP_NAME(sb, algorithm), \
                               sizeof(ECP_NAME(sb, algorithm))), \
        OSSL_PARAM_uint(OSSL_CAPABILITY_TLS_GROUP_ID, \
                        (unsigned int *)&oqs_group_list[idx].group_id_ecp_hyb), \
        OSSL_PARAM_uint(OSSL_CAPABILITY_TLS_GROUP_SECURITY_BITS, \
                        (unsigned int *)&oqs_group_list[idx].secbits), \
        OSSL_PARAM_int(OSSL_CAPABILITY_TLS_GROUP_MIN_TLS, \
                        (unsigned int *)&oqs_group_list[idx].mintls), \
        OSSL_PARAM_int(OSSL_CAPABILITY_TLS_GROUP_MAX_TLS, \
                        (unsigned int *)&oqs_group_list[idx].maxtls), \
        OSSL_PARAM_int(OSSL_CAPABILITY_TLS_GROUP_MIN_DTLS, \
                        (unsigned int *)&oqs_group_list[idx].mindtls), \
        OSSL_PARAM_int(OSSL_CAPABILITY_TLS_GROUP_MAX_DTLS, \
                        (unsigned int *)&oqs_group_list[idx].maxdtls), \
        OSSL_PARAM_int(OSSL_CAPABILITY_TLS_GROUP_IS_KEM, \
                        (unsigned int *)&oqs_group_list[idx].is_kem), \
        OSSL_PARAM_END \
    }

#define OQS_GROUP_ENTRY_ECX(tlsname, realname, algorithm, sb, idx) \
    { \
        OSSL_PARAM_utf8_string(OSSL_CAPABILITY_TLS_GROUP_NAME, \
                               ECX_NAME(sb, tlsname), \
                               sizeof(ECX_NAME(sb, tlsname))), \
        OSSL_PARAM_utf8_string(OSSL_CAPABILITY_TLS_GROUP_NAME_INTERNAL, \
                               ECX_NAME(sb, realname), \
                               sizeof(ECX_NAME(sb, realname))), \
        OSSL_PARAM_utf8_string(OSSL_CAPABILITY_TLS_GROUP_ALG, \
                               ECX_NAME(sb, algorithm), \
                               sizeof(ECX_NAME(sb, algorithm))), \
        OSSL_PARAM_uint(OSSL_CAPABILITY_TLS_GROUP_ID, \
                        (unsigned int *)&oqs_group_list[idx].group_id_ecx_hyb), \
        OSSL_PARAM_uint(OSSL_CAPABILITY_TLS_GROUP_SECURITY_BITS, \
                        (unsigned int *)&oqs_group_list[idx].secbits), \
        OSSL_PARAM_int(OSSL_CAPABILITY_TLS_GROUP_MIN_TLS, \
                        (unsigned int *)&oqs_group_list[idx].mintls), \
        OSSL_PARAM_int(OSSL_CAPABILITY_TLS_GROUP_MAX_TLS, \
                        (unsigned int *)&oqs_group_list[idx].maxtls), \
        OSSL_PARAM_int(OSSL_CAPABILITY_TLS_GROUP_MIN_DTLS, \
                        (unsigned int *)&oqs_group_list[idx].mindtls), \
        OSSL_PARAM_int(OSSL_CAPABILITY_TLS_GROUP_MAX_DTLS, \
                        (unsigned int *)&oqs_group_list[idx].maxdtls), \
        OSSL_PARAM_int(OSSL_CAPABILITY_TLS_GROUP_IS_KEM, \
                        (unsigned int *)&oqs_group_list[idx].is_kem), \
        OSSL_PARAM_END \
    }

static const OSSL_PARAM oqs_param_group_list[][11] = {
///// OQS_TEMPLATE_FRAGMENT_GROUP_NAMES_START

#ifdef OQS_ENABLE_KEM_frodokem_640_aes
    OQS_GROUP_ENTRY(frodo640aes, frodo640aes, frodo640aes, 128, 0),
    OQS_GROUP_ENTRY_ECP(frodo640aes, frodo640aes, frodo640aes, 128, 0),
    OQS_GROUP_ENTRY_ECX(frodo640aes, frodo640aes, frodo640aes, 128, 0),
#endif
#ifdef OQS_ENABLE_KEM_frodokem_640_shake
    OQS_GROUP_ENTRY(frodo640shake, frodo640shake, frodo640shake, 128, 1),
    OQS_GROUP_ENTRY_ECP(frodo640shake, frodo640shake, frodo640shake, 128, 1),
    OQS_GROUP_ENTRY_ECX(frodo640shake, frodo640shake, frodo640shake, 128, 1),
#endif
#ifdef OQS_ENABLE_KEM_frodokem_976_aes
    OQS_GROUP_ENTRY(frodo976aes, frodo976aes, frodo976aes, 192, 2),
    OQS_GROUP_ENTRY_ECP(frodo976aes, frodo976aes, frodo976aes, 192, 2),
    OQS_GROUP_ENTRY_ECX(frodo976aes, frodo976aes, frodo976aes, 192, 2),
#endif
#ifdef OQS_ENABLE_KEM_frodokem_976_shake
    OQS_GROUP_ENTRY(frodo976shake, frodo976shake, frodo976shake, 192, 3),
    OQS_GROUP_ENTRY_ECP(frodo976shake, frodo976shake, frodo976shake, 192, 3),
    OQS_GROUP_ENTRY_ECX(frodo976shake, frodo976shake, frodo976shake, 192, 3),
#endif
#ifdef OQS_ENABLE_KEM_frodokem_1344_aes
    OQS_GROUP_ENTRY(frodo1344aes, frodo1344aes, frodo1344aes, 256, 4),
    OQS_GROUP_ENTRY_ECP(frodo1344aes, frodo1344aes, frodo1344aes, 256, 4),
#endif
#ifdef OQS_ENABLE_KEM_frodokem_1344_shake
    OQS_GROUP_ENTRY(frodo1344shake, frodo1344shake, frodo1344shake, 256, 5),
    OQS_GROUP_ENTRY_ECP(frodo1344shake, frodo1344shake, frodo1344shake, 256, 5),
#endif
#ifdef OQS_ENABLE_KEM_kyber_512
    OQS_GROUP_ENTRY(kyber512, kyber512, kyber512, 128, 6),
    OQS_GROUP_ENTRY_ECP(kyber512, kyber512, kyber512, 128, 6),
    OQS_GROUP_ENTRY_ECX(kyber512, kyber512, kyber512, 128, 6),
#endif
#ifdef OQS_ENABLE_KEM_kyber_768
    OQS_GROUP_ENTRY(kyber768, kyber768, kyber768, 192, 7),
    OQS_GROUP_ENTRY_ECP(kyber768, kyber768, kyber768, 192, 7),
    OQS_GROUP_ENTRY_ECX(kyber768, kyber768, kyber768, 192, 7),
#endif
#ifdef OQS_ENABLE_KEM_kyber_1024
    OQS_GROUP_ENTRY(kyber1024, kyber1024, kyber1024, 256, 8),
    OQS_GROUP_ENTRY_ECP(kyber1024, kyber1024, kyber1024, 256, 8),
#endif
#ifdef OQS_ENABLE_KEM_bike_l1
    OQS_GROUP_ENTRY(bikel1, bikel1, bikel1, 128, 9),
    OQS_GROUP_ENTRY_ECP(bikel1, bikel1, bikel1, 128, 9),
    OQS_GROUP_ENTRY_ECX(bikel1, bikel1, bikel1, 128, 9),
#endif
#ifdef OQS_ENABLE_KEM_bike_l3
    OQS_GROUP_ENTRY(bikel3, bikel3, bikel3, 192, 10),
    OQS_GROUP_ENTRY_ECP(bikel3, bikel3, bikel3, 192, 10),
    OQS_GROUP_ENTRY_ECX(bikel3, bikel3, bikel3, 192, 10),
#endif
#ifdef OQS_ENABLE_KEM_bike_l5
    OQS_GROUP_ENTRY(bikel5, bikel5, bikel5, 256, 11),
    OQS_GROUP_ENTRY_ECP(bikel5, bikel5, bikel5, 256, 11),
#endif
#ifdef OQS_ENABLE_KEM_kyber_512_90s
    OQS_GROUP_ENTRY(kyber90s512, kyber90s512, kyber90s512, 128, 12),
    OQS_GROUP_ENTRY_ECP(kyber90s512, kyber90s512, kyber90s512, 128, 12),
    OQS_GROUP_ENTRY_ECX(kyber90s512, kyber90s512, kyber90s512, 128, 12),
#endif
#ifdef OQS_ENABLE_KEM_kyber_768_90s
    OQS_GROUP_ENTRY(kyber90s768, kyber90s768, kyber90s768, 192, 13),
    OQS_GROUP_ENTRY_ECP(kyber90s768, kyber90s768, kyber90s768, 192, 13),
    OQS_GROUP_ENTRY_ECX(kyber90s768, kyber90s768, kyber90s768, 192, 13),
#endif
#ifdef OQS_ENABLE_KEM_kyber_1024_90s
    OQS_GROUP_ENTRY(kyber90s1024, kyber90s1024, kyber90s1024, 256, 14),
    OQS_GROUP_ENTRY_ECP(kyber90s1024, kyber90s1024, kyber90s1024, 256, 14),
#endif
#ifdef OQS_ENABLE_KEM_hqc_128
    OQS_GROUP_ENTRY(hqc128, hqc128, hqc128, 128, 15),
    OQS_GROUP_ENTRY_ECP(hqc128, hqc128, hqc128, 128, 15),
    OQS_GROUP_ENTRY_ECX(hqc128, hqc128, hqc128, 128, 15),
#endif
#ifdef OQS_ENABLE_KEM_hqc_192
    OQS_GROUP_ENTRY(hqc192, hqc192, hqc192, 192, 16),
    OQS_GROUP_ENTRY_ECP(hqc192, hqc192, hqc192, 192, 16),
    OQS_GROUP_ENTRY_ECX(hqc192, hqc192, hqc192, 192, 16),
#endif
#ifdef OQS_ENABLE_KEM_hqc_256
    OQS_GROUP_ENTRY(hqc256, hqc256, hqc256, 256, 17),
    OQS_GROUP_ENTRY_ECP(hqc256, hqc256, hqc256, 256, 17),
#endif
///// OQS_TEMPLATE_FRAGMENT_GROUP_NAMES_END
};

typedef struct oqs_sigalg_constants_st {
    unsigned int code_point;         /* Code point */
    unsigned int secbits;            /* Bits of security */
    int mintls;                      /* Minimum TLS version, -1 unsupported */
    int maxtls;                      /* Maximum TLS version (or 0 for undefined) */
} OQS_SIGALG_CONSTANTS;

static OQS_SIGALG_CONSTANTS oqs_sigalg_list[] = {
    // ad-hoc assignments - take from OQS generate data structures
///// OQS_TEMPLATE_FRAGMENT_SIGALG_ASSIGNMENTS_START
    { 0xfea0, 128, TLS1_3_VERSION, 0 },
    { 0xfea1, 128, TLS1_3_VERSION, 0 },
    { 0xfea2, 128, TLS1_3_VERSION, 0 },
    { 0xfea3, 192, TLS1_3_VERSION, 0 },
    { 0xfea4, 192, TLS1_3_VERSION, 0 },
    { 0xfea5, 256, TLS1_3_VERSION, 0 },
    { 0xfea6, 256, TLS1_3_VERSION, 0 },
    { 0xfea7, 128, TLS1_3_VERSION, 0 },
    { 0xfea8, 128, TLS1_3_VERSION, 0 },
    { 0xfea9, 128, TLS1_3_VERSION, 0 },
    { 0xfeaa, 192, TLS1_3_VERSION, 0 },
    { 0xfeab, 192, TLS1_3_VERSION, 0 },
    { 0xfeac, 256, TLS1_3_VERSION, 0 },
    { 0xfead, 256, TLS1_3_VERSION, 0 },
    { 0xfeae, 128, TLS1_3_VERSION, 0 },
    { 0xfeaf, 128, TLS1_3_VERSION, 0 },
    { 0xfeb0, 128, TLS1_3_VERSION, 0 },
    { 0xfeb1, 256, TLS1_3_VERSION, 0 },
    { 0xfeb2, 256, TLS1_3_VERSION, 0 },
    { 0xfe42, 128, TLS1_3_VERSION, 0 },
    { 0xfe43, 128, TLS1_3_VERSION, 0 },
    { 0xfe44, 128, TLS1_3_VERSION, 0 },
    { 0xfe45, 128, TLS1_3_VERSION, 0 },
    { 0xfe46, 128, TLS1_3_VERSION, 0 },
    { 0xfe47, 128, TLS1_3_VERSION, 0 },
    { 0xfe5e, 128, TLS1_3_VERSION, 0 },
    { 0xfe5f, 128, TLS1_3_VERSION, 0 },
    { 0xfe60, 128, TLS1_3_VERSION, 0 },
    { 0xfe67, 128, TLS1_3_VERSION, 0 },
    { 0xfe68, 128, TLS1_3_VERSION, 0 },
    { 0xfe69, 128, TLS1_3_VERSION, 0 },
    { 0xfe7d, 128, TLS1_3_VERSION, 0 },
    { 0xfe7e, 128, TLS1_3_VERSION, 0 },
    { 0xfe7f, 128, TLS1_3_VERSION, 0 },
///// OQS_TEMPLATE_FRAGMENT_SIGALG_ASSIGNMENTS_END
};

int oqs_patch_codepoints() {
	
///// OQS_TEMPLATE_FRAGMENT_CODEPOINT_PATCHING_START


   if (getenv("OQS_CODEPOINT_FRODO640AES")) oqs_group_list[0].group_id = atoi(getenv("OQS_CODEPOINT_FRODO640AES"));
   if (getenv("OQS_CODEPOINT_P256_FRODO640AES")) oqs_group_list[0].group_id_ecp_hyb = atoi(getenv("OQS_CODEPOINT_P256_FRODO640AES"));
   if (getenv("OQS_CODEPOINT_X25519_FRODO640AES")) oqs_group_list[0].group_id_ecx_hyb = atoi(getenv("OQS_CODEPOINT_X25519_FRODO640AES"));
   if (getenv("OQS_CODEPOINT_FRODO640SHAKE")) oqs_group_list[1].group_id = atoi(getenv("OQS_CODEPOINT_FRODO640SHAKE"));
   if (getenv("OQS_CODEPOINT_P256_FRODO640SHAKE")) oqs_group_list[1].group_id_ecp_hyb = atoi(getenv("OQS_CODEPOINT_P256_FRODO640SHAKE"));
   if (getenv("OQS_CODEPOINT_X25519_FRODO640SHAKE")) oqs_group_list[1].group_id_ecx_hyb = atoi(getenv("OQS_CODEPOINT_X25519_FRODO640SHAKE"));
   if (getenv("OQS_CODEPOINT_FRODO976AES")) oqs_group_list[2].group_id = atoi(getenv("OQS_CODEPOINT_FRODO976AES"));
   if (getenv("OQS_CODEPOINT_P384_FRODO976AES")) oqs_group_list[2].group_id_ecp_hyb = atoi(getenv("OQS_CODEPOINT_P384_FRODO976AES"));
   if (getenv("OQS_CODEPOINT_X448_FRODO976AES")) oqs_group_list[2].group_id_ecx_hyb = atoi(getenv("OQS_CODEPOINT_X448_FRODO976AES"));
   if (getenv("OQS_CODEPOINT_FRODO976SHAKE")) oqs_group_list[3].group_id = atoi(getenv("OQS_CODEPOINT_FRODO976SHAKE"));
   if (getenv("OQS_CODEPOINT_P384_FRODO976SHAKE")) oqs_group_list[3].group_id_ecp_hyb = atoi(getenv("OQS_CODEPOINT_P384_FRODO976SHAKE"));
   if (getenv("OQS_CODEPOINT_X448_FRODO976SHAKE")) oqs_group_list[3].group_id_ecx_hyb = atoi(getenv("OQS_CODEPOINT_X448_FRODO976SHAKE"));
   if (getenv("OQS_CODEPOINT_FRODO1344AES")) oqs_group_list[4].group_id = atoi(getenv("OQS_CODEPOINT_FRODO1344AES"));
   if (getenv("OQS_CODEPOINT_P521_FRODO1344AES")) oqs_group_list[4].group_id_ecp_hyb = atoi(getenv("OQS_CODEPOINT_P521_FRODO1344AES"));
   if (getenv("OQS_CODEPOINT_FRODO1344SHAKE")) oqs_group_list[5].group_id = atoi(getenv("OQS_CODEPOINT_FRODO1344SHAKE"));
   if (getenv("OQS_CODEPOINT_P521_FRODO1344SHAKE")) oqs_group_list[5].group_id_ecp_hyb = atoi(getenv("OQS_CODEPOINT_P521_FRODO1344SHAKE"));
   if (getenv("OQS_CODEPOINT_KYBER512")) oqs_group_list[6].group_id = atoi(getenv("OQS_CODEPOINT_KYBER512"));
   if (getenv("OQS_CODEPOINT_P256_KYBER512")) oqs_group_list[6].group_id_ecp_hyb = atoi(getenv("OQS_CODEPOINT_P256_KYBER512"));
   if (getenv("OQS_CODEPOINT_X25519_KYBER512")) oqs_group_list[6].group_id_ecx_hyb = atoi(getenv("OQS_CODEPOINT_X25519_KYBER512"));
   if (getenv("OQS_CODEPOINT_KYBER768")) oqs_group_list[7].group_id = atoi(getenv("OQS_CODEPOINT_KYBER768"));
   if (getenv("OQS_CODEPOINT_P384_KYBER768")) oqs_group_list[7].group_id_ecp_hyb = atoi(getenv("OQS_CODEPOINT_P384_KYBER768"));
   if (getenv("OQS_CODEPOINT_X448_KYBER768")) oqs_group_list[7].group_id_ecx_hyb = atoi(getenv("OQS_CODEPOINT_X448_KYBER768"));
   if (getenv("OQS_CODEPOINT_KYBER1024")) oqs_group_list[8].group_id = atoi(getenv("OQS_CODEPOINT_KYBER1024"));
   if (getenv("OQS_CODEPOINT_P521_KYBER1024")) oqs_group_list[8].group_id_ecp_hyb = atoi(getenv("OQS_CODEPOINT_P521_KYBER1024"));
   if (getenv("OQS_CODEPOINT_BIKEL1")) oqs_group_list[9].group_id = atoi(getenv("OQS_CODEPOINT_BIKEL1"));
   if (getenv("OQS_CODEPOINT_P256_BIKEL1")) oqs_group_list[9].group_id_ecp_hyb = atoi(getenv("OQS_CODEPOINT_P256_BIKEL1"));
   if (getenv("OQS_CODEPOINT_X25519_BIKEL1")) oqs_group_list[9].group_id_ecx_hyb = atoi(getenv("OQS_CODEPOINT_X25519_BIKEL1"));
   if (getenv("OQS_CODEPOINT_BIKEL3")) oqs_group_list[10].group_id = atoi(getenv("OQS_CODEPOINT_BIKEL3"));
   if (getenv("OQS_CODEPOINT_P384_BIKEL3")) oqs_group_list[10].group_id_ecp_hyb = atoi(getenv("OQS_CODEPOINT_P384_BIKEL3"));
   if (getenv("OQS_CODEPOINT_X448_BIKEL3")) oqs_group_list[10].group_id_ecx_hyb = atoi(getenv("OQS_CODEPOINT_X448_BIKEL3"));
   if (getenv("OQS_CODEPOINT_BIKEL5")) oqs_group_list[11].group_id = atoi(getenv("OQS_CODEPOINT_BIKEL5"));
   if (getenv("OQS_CODEPOINT_P521_BIKEL5")) oqs_group_list[11].group_id_ecp_hyb = atoi(getenv("OQS_CODEPOINT_P521_BIKEL5"));
   if (getenv("OQS_CODEPOINT_KYBER90S512")) oqs_group_list[12].group_id = atoi(getenv("OQS_CODEPOINT_KYBER90S512"));
   if (getenv("OQS_CODEPOINT_P256_KYBER90S512")) oqs_group_list[12].group_id_ecp_hyb = atoi(getenv("OQS_CODEPOINT_P256_KYBER90S512"));
   if (getenv("OQS_CODEPOINT_X25519_KYBER90S512")) oqs_group_list[12].group_id_ecx_hyb = atoi(getenv("OQS_CODEPOINT_X25519_KYBER90S512"));
   if (getenv("OQS_CODEPOINT_KYBER90S768")) oqs_group_list[13].group_id = atoi(getenv("OQS_CODEPOINT_KYBER90S768"));
   if (getenv("OQS_CODEPOINT_P384_KYBER90S768")) oqs_group_list[13].group_id_ecp_hyb = atoi(getenv("OQS_CODEPOINT_P384_KYBER90S768"));
   if (getenv("OQS_CODEPOINT_X448_KYBER90S768")) oqs_group_list[13].group_id_ecx_hyb = atoi(getenv("OQS_CODEPOINT_X448_KYBER90S768"));
   if (getenv("OQS_CODEPOINT_KYBER90S1024")) oqs_group_list[14].group_id = atoi(getenv("OQS_CODEPOINT_KYBER90S1024"));
   if (getenv("OQS_CODEPOINT_P521_KYBER90S1024")) oqs_group_list[14].group_id_ecp_hyb = atoi(getenv("OQS_CODEPOINT_P521_KYBER90S1024"));
   if (getenv("OQS_CODEPOINT_HQC128")) oqs_group_list[15].group_id = atoi(getenv("OQS_CODEPOINT_HQC128"));
   if (getenv("OQS_CODEPOINT_P256_HQC128")) oqs_group_list[15].group_id_ecp_hyb = atoi(getenv("OQS_CODEPOINT_P256_HQC128"));
   if (getenv("OQS_CODEPOINT_X25519_HQC128")) oqs_group_list[15].group_id_ecx_hyb = atoi(getenv("OQS_CODEPOINT_X25519_HQC128"));
   if (getenv("OQS_CODEPOINT_HQC192")) oqs_group_list[16].group_id = atoi(getenv("OQS_CODEPOINT_HQC192"));
   if (getenv("OQS_CODEPOINT_P384_HQC192")) oqs_group_list[16].group_id_ecp_hyb = atoi(getenv("OQS_CODEPOINT_P384_HQC192"));
   if (getenv("OQS_CODEPOINT_X448_HQC192")) oqs_group_list[16].group_id_ecx_hyb = atoi(getenv("OQS_CODEPOINT_X448_HQC192"));
   if (getenv("OQS_CODEPOINT_HQC256")) oqs_group_list[17].group_id = atoi(getenv("OQS_CODEPOINT_HQC256"));
   if (getenv("OQS_CODEPOINT_P521_HQC256")) oqs_group_list[17].group_id_ecp_hyb = atoi(getenv("OQS_CODEPOINT_P521_HQC256"));

   if (getenv("OQS_CODEPOINT_DILITHIUM2")) oqs_sigalg_list[0].code_point = atoi(getenv("OQS_CODEPOINT_DILITHIUM2"));
   if (getenv("OQS_CODEPOINT_P256_DILITHIUM2")) oqs_sigalg_list[1].code_point = atoi(getenv("OQS_CODEPOINT_P256_DILITHIUM2"));
   if (getenv("OQS_CODEPOINT_RSA3072_DILITHIUM2")) oqs_sigalg_list[2].code_point = atoi(getenv("OQS_CODEPOINT_RSA3072_DILITHIUM2"));
   if (getenv("OQS_CODEPOINT_DILITHIUM3")) oqs_sigalg_list[3].code_point = atoi(getenv("OQS_CODEPOINT_DILITHIUM3"));
   if (getenv("OQS_CODEPOINT_P384_DILITHIUM3")) oqs_sigalg_list[4].code_point = atoi(getenv("OQS_CODEPOINT_P384_DILITHIUM3"));
   if (getenv("OQS_CODEPOINT_DILITHIUM5")) oqs_sigalg_list[5].code_point = atoi(getenv("OQS_CODEPOINT_DILITHIUM5"));
   if (getenv("OQS_CODEPOINT_P521_DILITHIUM5")) oqs_sigalg_list[6].code_point = atoi(getenv("OQS_CODEPOINT_P521_DILITHIUM5"));
   if (getenv("OQS_CODEPOINT_DILITHIUM2_AES")) oqs_sigalg_list[7].code_point = atoi(getenv("OQS_CODEPOINT_DILITHIUM2_AES"));
   if (getenv("OQS_CODEPOINT_P256_DILITHIUM2_AES")) oqs_sigalg_list[8].code_point = atoi(getenv("OQS_CODEPOINT_P256_DILITHIUM2_AES"));
   if (getenv("OQS_CODEPOINT_RSA3072_DILITHIUM2_AES")) oqs_sigalg_list[9].code_point = atoi(getenv("OQS_CODEPOINT_RSA3072_DILITHIUM2_AES"));
   if (getenv("OQS_CODEPOINT_DILITHIUM3_AES")) oqs_sigalg_list[10].code_point = atoi(getenv("OQS_CODEPOINT_DILITHIUM3_AES"));
   if (getenv("OQS_CODEPOINT_P384_DILITHIUM3_AES")) oqs_sigalg_list[11].code_point = atoi(getenv("OQS_CODEPOINT_P384_DILITHIUM3_AES"));
   if (getenv("OQS_CODEPOINT_DILITHIUM5_AES")) oqs_sigalg_list[12].code_point = atoi(getenv("OQS_CODEPOINT_DILITHIUM5_AES"));
   if (getenv("OQS_CODEPOINT_P521_DILITHIUM5_AES")) oqs_sigalg_list[13].code_point = atoi(getenv("OQS_CODEPOINT_P521_DILITHIUM5_AES"));
   if (getenv("OQS_CODEPOINT_FALCON512")) oqs_sigalg_list[14].code_point = atoi(getenv("OQS_CODEPOINT_FALCON512"));
   if (getenv("OQS_CODEPOINT_P256_FALCON512")) oqs_sigalg_list[15].code_point = atoi(getenv("OQS_CODEPOINT_P256_FALCON512"));
   if (getenv("OQS_CODEPOINT_RSA3072_FALCON512")) oqs_sigalg_list[16].code_point = atoi(getenv("OQS_CODEPOINT_RSA3072_FALCON512"));
   if (getenv("OQS_CODEPOINT_FALCON1024")) oqs_sigalg_list[17].code_point = atoi(getenv("OQS_CODEPOINT_FALCON1024"));
   if (getenv("OQS_CODEPOINT_P521_FALCON1024")) oqs_sigalg_list[18].code_point = atoi(getenv("OQS_CODEPOINT_P521_FALCON1024"));
   if (getenv("OQS_CODEPOINT_SPHINCSHARAKA128FROBUST")) oqs_sigalg_list[19].code_point = atoi(getenv("OQS_CODEPOINT_SPHINCSHARAKA128FROBUST"));
   if (getenv("OQS_CODEPOINT_P256_SPHINCSHARAKA128FROBUST")) oqs_sigalg_list[20].code_point = atoi(getenv("OQS_CODEPOINT_P256_SPHINCSHARAKA128FROBUST"));
   if (getenv("OQS_CODEPOINT_RSA3072_SPHINCSHARAKA128FROBUST")) oqs_sigalg_list[21].code_point = atoi(getenv("OQS_CODEPOINT_RSA3072_SPHINCSHARAKA128FROBUST"));
   if (getenv("OQS_CODEPOINT_SPHINCSHARAKA128FSIMPLE")) oqs_sigalg_list[22].code_point = atoi(getenv("OQS_CODEPOINT_SPHINCSHARAKA128FSIMPLE"));
   if (getenv("OQS_CODEPOINT_P256_SPHINCSHARAKA128FSIMPLE")) oqs_sigalg_list[23].code_point = atoi(getenv("OQS_CODEPOINT_P256_SPHINCSHARAKA128FSIMPLE"));
   if (getenv("OQS_CODEPOINT_RSA3072_SPHINCSHARAKA128FSIMPLE")) oqs_sigalg_list[24].code_point = atoi(getenv("OQS_CODEPOINT_RSA3072_SPHINCSHARAKA128FSIMPLE"));
   if (getenv("OQS_CODEPOINT_SPHINCSSHA256128FROBUST")) oqs_sigalg_list[25].code_point = atoi(getenv("OQS_CODEPOINT_SPHINCSSHA256128FROBUST"));
   if (getenv("OQS_CODEPOINT_P256_SPHINCSSHA256128FROBUST")) oqs_sigalg_list[26].code_point = atoi(getenv("OQS_CODEPOINT_P256_SPHINCSSHA256128FROBUST"));
   if (getenv("OQS_CODEPOINT_RSA3072_SPHINCSSHA256128FROBUST")) oqs_sigalg_list[27].code_point = atoi(getenv("OQS_CODEPOINT_RSA3072_SPHINCSSHA256128FROBUST"));
   if (getenv("OQS_CODEPOINT_SPHINCSSHA256128SSIMPLE")) oqs_sigalg_list[28].code_point = atoi(getenv("OQS_CODEPOINT_SPHINCSSHA256128SSIMPLE"));
   if (getenv("OQS_CODEPOINT_P256_SPHINCSSHA256128SSIMPLE")) oqs_sigalg_list[29].code_point = atoi(getenv("OQS_CODEPOINT_P256_SPHINCSSHA256128SSIMPLE"));
   if (getenv("OQS_CODEPOINT_RSA3072_SPHINCSSHA256128SSIMPLE")) oqs_sigalg_list[30].code_point = atoi(getenv("OQS_CODEPOINT_RSA3072_SPHINCSSHA256128SSIMPLE"));
   if (getenv("OQS_CODEPOINT_SPHINCSSHAKE256128FSIMPLE")) oqs_sigalg_list[31].code_point = atoi(getenv("OQS_CODEPOINT_SPHINCSSHAKE256128FSIMPLE"));
   if (getenv("OQS_CODEPOINT_P256_SPHINCSSHAKE256128FSIMPLE")) oqs_sigalg_list[32].code_point = atoi(getenv("OQS_CODEPOINT_P256_SPHINCSSHAKE256128FSIMPLE"));
   if (getenv("OQS_CODEPOINT_RSA3072_SPHINCSSHAKE256128FSIMPLE")) oqs_sigalg_list[33].code_point = atoi(getenv("OQS_CODEPOINT_RSA3072_SPHINCSSHAKE256128FSIMPLE"));
///// OQS_TEMPLATE_FRAGMENT_CODEPOINT_PATCHING_END
	return 1;
}

static int oqs_group_capability(OSSL_CALLBACK *cb, void *arg)
{
    size_t i;

    for (i = 0; i < OSSL_NELEM(oqs_param_group_list); i++) {
        if (!cb(oqs_param_group_list[i], arg))
            return 0;
    }

    return 1;
}

#ifdef OSSL_CAPABILITY_TLS_SIGALG_NAME
#define OQS_SIGALG_ENTRY(tlsname, realname, algorithm, oid, idx) \
    { \
        OSSL_PARAM_utf8_string(OSSL_CAPABILITY_TLS_SIGALG_IANA_NAME, \
                               #tlsname, \
                               sizeof(#tlsname)), \
        OSSL_PARAM_utf8_string(OSSL_CAPABILITY_TLS_SIGALG_NAME, \
                               #tlsname, \
                               sizeof(#tlsname)), \
        OSSL_PARAM_utf8_string(OSSL_CAPABILITY_TLS_SIGALG_OID, \
                               #oid, \
                               sizeof(#oid)), \
        OSSL_PARAM_uint(OSSL_CAPABILITY_TLS_SIGALG_CODE_POINT, \
                        (unsigned int *)&oqs_sigalg_list[idx].code_point), \
        OSSL_PARAM_uint(OSSL_CAPABILITY_TLS_SIGALG_SECURITY_BITS, \
                        (unsigned int *)&oqs_sigalg_list[idx].secbits), \
        OSSL_PARAM_int(OSSL_CAPABILITY_TLS_SIGALG_MIN_TLS, \
                        (unsigned int *)&oqs_sigalg_list[idx].mintls), \
        OSSL_PARAM_int(OSSL_CAPABILITY_TLS_SIGALG_MAX_TLS, \
                        (unsigned int *)&oqs_sigalg_list[idx].maxtls), \
        OSSL_PARAM_END \
    }

static const OSSL_PARAM oqs_param_sigalg_list[][12] = {
///// OQS_TEMPLATE_FRAGMENT_SIGALG_NAMES_START
#ifdef OQS_ENABLE_SIG_dilithium_2
    OQS_SIGALG_ENTRY(dilithium2, dilithium2, dilithium2, "1.3.6.1.4.1.2.267.7.4.4", 0),
    OQS_SIGALG_ENTRY(p256_dilithium2, p256_dilithium2, p256_dilithium2, "1.3.9999.2.7.1", 1),
    OQS_SIGALG_ENTRY(rsa3072_dilithium2, rsa3072_dilithium2, rsa3072_dilithium2, "1.3.9999.2.7.2", 2),
#endif
#ifdef OQS_ENABLE_SIG_dilithium_3
    OQS_SIGALG_ENTRY(dilithium3, dilithium3, dilithium3, "1.3.6.1.4.1.2.267.7.6.5", 3),
    OQS_SIGALG_ENTRY(p384_dilithium3, p384_dilithium3, p384_dilithium3, "1.3.9999.2.7.3", 4),
#endif
#ifdef OQS_ENABLE_SIG_dilithium_5
    OQS_SIGALG_ENTRY(dilithium5, dilithium5, dilithium5, "1.3.6.1.4.1.2.267.7.8.7", 5),
    OQS_SIGALG_ENTRY(p521_dilithium5, p521_dilithium5, p521_dilithium5, "1.3.9999.2.7.4", 6),
#endif
#ifdef OQS_ENABLE_SIG_dilithium_2_aes
    OQS_SIGALG_ENTRY(dilithium2_aes, dilithium2_aes, dilithium2_aes, "1.3.6.1.4.1.2.267.11.4.4", 7),
    OQS_SIGALG_ENTRY(p256_dilithium2_aes, p256_dilithium2_aes, p256_dilithium2_aes, "1.3.9999.2.11.1", 8),
    OQS_SIGALG_ENTRY(rsa3072_dilithium2_aes, rsa3072_dilithium2_aes, rsa3072_dilithium2_aes, "1.3.9999.2.11.2", 9),
#endif
#ifdef OQS_ENABLE_SIG_dilithium_3_aes
    OQS_SIGALG_ENTRY(dilithium3_aes, dilithium3_aes, dilithium3_aes, "1.3.6.1.4.1.2.267.11.6.5", 10),
    OQS_SIGALG_ENTRY(p384_dilithium3_aes, p384_dilithium3_aes, p384_dilithium3_aes, "1.3.9999.2.11.3", 11),
#endif
#ifdef OQS_ENABLE_SIG_dilithium_5_aes
    OQS_SIGALG_ENTRY(dilithium5_aes, dilithium5_aes, dilithium5_aes, "1.3.6.1.4.1.2.267.11.8.7", 12),
    OQS_SIGALG_ENTRY(p521_dilithium5_aes, p521_dilithium5_aes, p521_dilithium5_aes, "1.3.9999.2.11.4", 13),
#endif
#ifdef OQS_ENABLE_SIG_falcon_512
    OQS_SIGALG_ENTRY(falcon512, falcon512, falcon512, "1.3.9999.3.6", 14),
    OQS_SIGALG_ENTRY(p256_falcon512, p256_falcon512, p256_falcon512, "1.3.9999.3.7", 15),
    OQS_SIGALG_ENTRY(rsa3072_falcon512, rsa3072_falcon512, rsa3072_falcon512, "1.3.9999.3.8", 16),
#endif
#ifdef OQS_ENABLE_SIG_falcon_1024
    OQS_SIGALG_ENTRY(falcon1024, falcon1024, falcon1024, "1.3.9999.3.9", 17),
    OQS_SIGALG_ENTRY(p521_falcon1024, p521_falcon1024, p521_falcon1024, "1.3.9999.3.10", 18),
#endif
#ifdef OQS_ENABLE_SIG_sphincs_haraka_128f_robust
    OQS_SIGALG_ENTRY(sphincsharaka128frobust, sphincsharaka128frobust, sphincsharaka128frobust, "1.3.9999.6.1.1", 19),
    OQS_SIGALG_ENTRY(p256_sphincsharaka128frobust, p256_sphincsharaka128frobust, p256_sphincsharaka128frobust, "1.3.9999.6.1.2", 20),
    OQS_SIGALG_ENTRY(rsa3072_sphincsharaka128frobust, rsa3072_sphincsharaka128frobust, rsa3072_sphincsharaka128frobust, "1.3.9999.6.1.3", 21),
#endif
#ifdef OQS_ENABLE_SIG_sphincs_haraka_128f_simple
    OQS_SIGALG_ENTRY(sphincsharaka128fsimple, sphincsharaka128fsimple, sphincsharaka128fsimple, "1.3.9999.6.1.4", 22),
    OQS_SIGALG_ENTRY(p256_sphincsharaka128fsimple, p256_sphincsharaka128fsimple, p256_sphincsharaka128fsimple, "1.3.9999.6.1.5", 23),
    OQS_SIGALG_ENTRY(rsa3072_sphincsharaka128fsimple, rsa3072_sphincsharaka128fsimple, rsa3072_sphincsharaka128fsimple, "1.3.9999.6.1.6", 24),
#endif
#ifdef OQS_ENABLE_SIG_sphincs_sha256_128f_robust
    OQS_SIGALG_ENTRY(sphincssha256128frobust, sphincssha256128frobust, sphincssha256128frobust, "1.3.9999.6.4.1", 25),
    OQS_SIGALG_ENTRY(p256_sphincssha256128frobust, p256_sphincssha256128frobust, p256_sphincssha256128frobust, "1.3.9999.6.4.2", 26),
    OQS_SIGALG_ENTRY(rsa3072_sphincssha256128frobust, rsa3072_sphincssha256128frobust, rsa3072_sphincssha256128frobust, "1.3.9999.6.4.3", 27),
#endif
#ifdef OQS_ENABLE_SIG_sphincs_sha256_128s_simple
    OQS_SIGALG_ENTRY(sphincssha256128ssimple, sphincssha256128ssimple, sphincssha256128ssimple, "1.3.9999.6.4.10", 28),
    OQS_SIGALG_ENTRY(p256_sphincssha256128ssimple, p256_sphincssha256128ssimple, p256_sphincssha256128ssimple, "1.3.9999.6.4.11", 29),
    OQS_SIGALG_ENTRY(rsa3072_sphincssha256128ssimple, rsa3072_sphincssha256128ssimple, rsa3072_sphincssha256128ssimple, "1.3.9999.6.4.12", 30),
#endif
#ifdef OQS_ENABLE_SIG_sphincs_shake256_128f_simple
    OQS_SIGALG_ENTRY(sphincsshake256128fsimple, sphincsshake256128fsimple, sphincsshake256128fsimple, "1.3.9999.6.7.4", 31),
    OQS_SIGALG_ENTRY(p256_sphincsshake256128fsimple, p256_sphincsshake256128fsimple, p256_sphincsshake256128fsimple, "1.3.9999.6.7.5", 32),
    OQS_SIGALG_ENTRY(rsa3072_sphincsshake256128fsimple, rsa3072_sphincsshake256128fsimple, rsa3072_sphincsshake256128fsimple, "1.3.9999.6.7.6", 33),
#endif
///// OQS_TEMPLATE_FRAGMENT_SIGALG_NAMES_END
};

static int oqs_sigalg_capability(OSSL_CALLBACK *cb, void *arg)
{
    size_t i;

    // relaxed assertion for the case that not all algorithms are enabled in liboqs:
    assert(OSSL_NELEM(oqs_param_sigalg_list) <= OSSL_NELEM(oqs_sigalg_list));
    for (i = 0; i < OSSL_NELEM(oqs_param_sigalg_list); i++) {
        if (!cb(oqs_param_sigalg_list[i], arg))
            return 0;
    }

    return 1;
}
#endif /* OSSL_CAPABILITY_TLS_SIGALG_NAME */

int oqs_provider_get_capabilities(void *provctx, const char *capability,
                              OSSL_CALLBACK *cb, void *arg)
{
    if (strcasecmp(capability, "TLS-GROUP") == 0)
        return oqs_group_capability(cb, arg);

#ifdef OSSL_CAPABILITY_TLS_SIGALG_NAME
    if (strcasecmp(capability, "TLS-SIGALG") == 0)
        return oqs_sigalg_capability(cb, arg);
#endif

    /* We don't support this capability */
    return 0;
}

