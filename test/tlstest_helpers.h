// SPDX-License-Identifier: Apache-2.0 AND MIT

int create_tls1_3_ctx_pair(OSSL_LIB_CTX *libctx, SSL_CTX **sctx, SSL_CTX **cctx,
                           char *certfile, char *privkeyfile);

int create_tls_objects(SSL_CTX *serverctx, SSL_CTX *clientctx, SSL **sssl,
                       SSL **cssl);

int create_tls_connection(SSL *serverssl, SSL *clientssl, int want);
