/*
 * Copyright 2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/core_numbers.h>
#include <openssl/core_names.h>
#include <openssl/params.h>
#include <openssl/engine.h>
#include <openssl/evp.h>
#include <openssl/cmac.h>

#include "prov/implementations.h"
#include "prov/provider_ctx.h"
#include "prov/provider_util.h"

/*
 * Forward declaration of everything implemented here.  This is not strictly
 * necessary for the compiler, but provides an assurance that the signatures
 * of the functions in the dispatch table are correct.
 */
static OSSL_OP_mac_newctx_fn cmac_new;
static OSSL_OP_mac_dupctx_fn cmac_dup;
static OSSL_OP_mac_freectx_fn cmac_free;
static OSSL_OP_mac_gettable_ctx_params_fn cmac_gettable_ctx_params;
static OSSL_OP_mac_get_ctx_params_fn cmac_get_ctx_params;
static OSSL_OP_mac_settable_ctx_params_fn cmac_settable_ctx_params;
static OSSL_OP_mac_set_ctx_params_fn cmac_set_ctx_params;
static OSSL_OP_mac_init_fn cmac_init;
static OSSL_OP_mac_update_fn cmac_update;
static OSSL_OP_mac_final_fn cmac_final;

/* local CMAC data */

struct cmac_data_st {
    void *provctx;
    CMAC_CTX *ctx;
    PROV_CIPHER cipher;
};

static void *cmac_new(void *provctx)
{
    struct cmac_data_st *macctx;

    if ((macctx = OPENSSL_zalloc(sizeof(*macctx))) == NULL
        || (macctx->ctx = CMAC_CTX_new()) == NULL) {
        OPENSSL_free(macctx);
        macctx = NULL;
    } else {
        macctx->provctx = provctx;
    }

    return macctx;
}

static void cmac_free(void *vmacctx)
{
    struct cmac_data_st *macctx = vmacctx;

    if (macctx != NULL) {
        CMAC_CTX_free(macctx->ctx);
        ossl_prov_cipher_reset(&macctx->cipher);
        OPENSSL_free(macctx);
    }
}

static void *cmac_dup(void *vsrc)
{
    struct cmac_data_st *src = vsrc;
    struct cmac_data_st *dst = cmac_new(src->provctx);

    if (!CMAC_CTX_copy(dst->ctx, src->ctx)
        || !ossl_prov_cipher_copy(&dst->cipher, &src->cipher)) {
        cmac_free(dst);
        return NULL;
    }
    return dst;
}

static size_t cmac_size(void *vmacctx)
{
    struct cmac_data_st *macctx = vmacctx;

    return EVP_CIPHER_CTX_block_size(CMAC_CTX_get0_cipher_ctx(macctx->ctx));
}

static int cmac_init(void *vmacctx)
{
    struct cmac_data_st *macctx = vmacctx;
    int rv = CMAC_Init(macctx->ctx, NULL, 0,
                       ossl_prov_cipher_cipher(&macctx->cipher),
                       ossl_prov_cipher_engine(&macctx->cipher));

    ossl_prov_cipher_reset(&macctx->cipher);
    return rv;
}

static int cmac_update(void *vmacctx, const unsigned char *data,
                       size_t datalen)
{
    struct cmac_data_st *macctx = vmacctx;

    return CMAC_Update(macctx->ctx, data, datalen);
}

static int cmac_final(void *vmacctx, unsigned char *out, size_t *outl,
                      size_t outsize)
{
    struct cmac_data_st *macctx = vmacctx;

    return CMAC_Final(macctx->ctx, out, outl);
}

static const OSSL_PARAM known_gettable_ctx_params[] = {
    OSSL_PARAM_size_t(OSSL_MAC_PARAM_SIZE, NULL),
    OSSL_PARAM_END
};
static const OSSL_PARAM *cmac_gettable_ctx_params(void)
{
    return known_gettable_ctx_params;
}

static int cmac_get_ctx_params(void *vmacctx, OSSL_PARAM params[])
{
    OSSL_PARAM *p;

    if ((p = OSSL_PARAM_locate(params, OSSL_MAC_PARAM_SIZE)) != NULL)
        return OSSL_PARAM_set_size_t(p, cmac_size(vmacctx));

    return 1;
}

static const OSSL_PARAM known_settable_ctx_params[] = {
    OSSL_PARAM_utf8_string(OSSL_MAC_PARAM_CIPHER, NULL, 0),
    OSSL_PARAM_utf8_string(OSSL_MAC_PARAM_PROPERTIES, NULL, 0),
    OSSL_PARAM_octet_string(OSSL_MAC_PARAM_KEY, NULL, 0),
    OSSL_PARAM_END
};
static const OSSL_PARAM *cmac_settable_ctx_params(void)
{
    return known_settable_ctx_params;
}

/*
 * ALL parameters should be set before init().
 */
static int cmac_set_ctx_params(void *vmacctx, const OSSL_PARAM params[])
{
    struct cmac_data_st *macctx = vmacctx;
    OPENSSL_CTX *ctx = PROV_LIBRARY_CONTEXT_OF(macctx->provctx);
    const OSSL_PARAM *p;

    if (!ossl_prov_cipher_load_from_params(&macctx->cipher, params, ctx))
        return 0;

    if ((p = OSSL_PARAM_locate_const(params, OSSL_MAC_PARAM_KEY)) != NULL) {
        if (p->data_type != OSSL_PARAM_OCTET_STRING)
            return 0;

        if (!CMAC_Init(macctx->ctx, p->data, p->data_size,
                       ossl_prov_cipher_cipher(&macctx->cipher),
                       ossl_prov_cipher_engine(&macctx->cipher)))
            return 0;

        ossl_prov_cipher_reset(&macctx->cipher);
    }
    return 1;
}

const OSSL_DISPATCH cmac_functions[] = {
    { OSSL_FUNC_MAC_NEWCTX, (void (*)(void))cmac_new },
    { OSSL_FUNC_MAC_DUPCTX, (void (*)(void))cmac_dup },
    { OSSL_FUNC_MAC_FREECTX, (void (*)(void))cmac_free },
    { OSSL_FUNC_MAC_INIT, (void (*)(void))cmac_init },
    { OSSL_FUNC_MAC_UPDATE, (void (*)(void))cmac_update },
    { OSSL_FUNC_MAC_FINAL, (void (*)(void))cmac_final },
    { OSSL_FUNC_MAC_GETTABLE_CTX_PARAMS,
      (void (*)(void))cmac_gettable_ctx_params },
    { OSSL_FUNC_MAC_GET_CTX_PARAMS, (void (*)(void))cmac_get_ctx_params },
    { OSSL_FUNC_MAC_SETTABLE_CTX_PARAMS,
      (void (*)(void))cmac_settable_ctx_params },
    { OSSL_FUNC_MAC_SET_CTX_PARAMS, (void (*)(void))cmac_set_ctx_params },
    { 0, NULL }
};