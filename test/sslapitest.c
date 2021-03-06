/*
 * Copyright 2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/opensslconf.h>
#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>

#include "ssltestlib.h"
#include "testutil.h"

static char *cert = NULL;
static char *privkey = NULL;

static int test_tlsext_status_type(void)
{
    SSL_CTX *ctx = NULL;
    SSL *con = NULL;
    int testresult = 0;

    /* Test tlsext_status_type */
    ctx = SSL_CTX_new(TLS_method());

    if (SSL_CTX_get_tlsext_status_type(ctx) != -1) {
        printf("Unexpected initial value for "
               "SSL_CTX_get_tlsext_status_type()\n");
        goto end;
    }

    con = SSL_new(ctx);

    if (SSL_get_tlsext_status_type(con) != -1) {
        printf("Unexpected initial value for SSL_get_tlsext_status_type()\n");
        goto end;
    }

    if (!SSL_set_tlsext_status_type(con, TLSEXT_STATUSTYPE_ocsp)) {
        printf("Unexpected fail for SSL_set_tlsext_status_type()\n");
        goto end;
    }

    if (SSL_get_tlsext_status_type(con) != TLSEXT_STATUSTYPE_ocsp) {
        printf("Unexpected result for SSL_get_tlsext_status_type()\n");
        goto end;
    }

    SSL_free(con);
    con = NULL;

    if (!SSL_CTX_set_tlsext_status_type(ctx, TLSEXT_STATUSTYPE_ocsp)) {
        printf("Unexpected fail for SSL_CTX_set_tlsext_status_type()\n");
        goto end;
    }

    if (SSL_CTX_get_tlsext_status_type(ctx) != TLSEXT_STATUSTYPE_ocsp) {
        printf("Unexpected result for SSL_CTX_get_tlsext_status_type()\n");
        goto end;
    }

    con = SSL_new(ctx);

    if (SSL_get_tlsext_status_type(con) != TLSEXT_STATUSTYPE_ocsp) {
        printf("Unexpected result for SSL_get_tlsext_status_type() (test 2)\n");
        goto end;
    }

    testresult = 1;

 end:
    SSL_free(con);
    SSL_CTX_free(ctx);

    return testresult;
}

typedef struct ssl_session_test_fixture {
    const char *test_case_name;
    int use_ext_cache;
    int use_int_cache;
} SSL_SESSION_TEST_FIXTURE;

static int new_called = 0, remove_called = 0;

static SSL_SESSION_TEST_FIXTURE
ssl_session_set_up(const char *const test_case_name)
{
    SSL_SESSION_TEST_FIXTURE fixture;

    fixture.test_case_name = test_case_name;
    fixture.use_ext_cache = 1;
    fixture.use_int_cache = 1;

    new_called = remove_called = 0;

    return fixture;
}

static void ssl_session_tear_down(SSL_SESSION_TEST_FIXTURE fixture)
{
}

static int new_session_cb(SSL *ssl, SSL_SESSION *sess)
{
    new_called++;

    return 1;
}

static void remove_session_cb(SSL_CTX *ctx, SSL_SESSION *sess)
{
    remove_called++;
}

static int execute_test_session(SSL_SESSION_TEST_FIXTURE fix)
{
    SSL_CTX *sctx = NULL, *cctx = NULL;
    SSL *serverssl1 = NULL, *clientssl1 = NULL;
    SSL *serverssl2 = NULL, *clientssl2 = NULL;
    SSL *serverssl3 = NULL, *clientssl3 = NULL;
    SSL_SESSION *sess1 = NULL, *sess2 = NULL;
    int testresult = 0;

    if (!create_ssl_ctx_pair(TLS_server_method(), TLS_client_method(), &sctx,
                             &cctx, cert, privkey)) {
        printf("Unable to create SSL_CTX pair\n");
        return 0;
    }

#ifndef OPENSSL_NO_TLS1_2
    /* Only allow TLS1.2 so we can force a connection failure later */
    SSL_CTX_set_min_proto_version(cctx, TLS1_2_VERSION);
#endif

    /* Set up session cache */
    if (fix.use_ext_cache) {
        SSL_CTX_sess_set_new_cb(cctx, new_session_cb);
        SSL_CTX_sess_set_remove_cb(cctx, remove_session_cb);
    }
    if (fix.use_int_cache) {
        /* Also covers instance where both are set */
        SSL_CTX_set_session_cache_mode(cctx, SSL_SESS_CACHE_CLIENT);
    } else {
        SSL_CTX_set_session_cache_mode(cctx,
                                       SSL_SESS_CACHE_CLIENT
                                       | SSL_SESS_CACHE_NO_INTERNAL_STORE);
    }

    if (!create_ssl_connection(sctx, cctx, &serverssl1, &clientssl1, NULL,
                               NULL)) {
        printf("Unable to create SSL connection\n");
        goto end;
    }
    sess1 = SSL_get1_session(clientssl1);
    if (sess1 == NULL) {
        printf("Unexpected NULL session\n");
        goto end;
    }

    if (fix.use_int_cache && SSL_CTX_add_session(cctx, sess1)) {
        /* Should have failed because it should already be in the cache */
        printf("Unexpected success adding session to cache\n");
        goto end;
    }

    if (fix.use_ext_cache && (new_called != 1 || remove_called != 0)) {
        printf("Session not added to cache\n");
        goto end;
    }

    if (!create_ssl_connection(sctx, cctx, &serverssl2, &clientssl2, NULL,
                               NULL)) {
        printf("Unable to create second SSL connection\n");
        goto end;
    }

    sess2 = SSL_get1_session(clientssl2);
    if (sess2 == NULL) {
        printf("Unexpected NULL session from clientssl2\n");
        goto end;
    }

    if (fix.use_ext_cache && (new_called != 2 || remove_called != 0)) {
        printf("Remove session callback unexpectedly called\n");
        goto end;
    }

    /*
     * This should clear sess2 from the cache because it is a "bad" session. See
     * SSL_set_session() documentation.
     */
    if (!SSL_set_session(clientssl2, sess1)) {
        printf("Unexpected failure setting session\n");
        goto end;
    }

    if (fix.use_ext_cache && (new_called != 2 || remove_called != 1)) {
        printf("Failed to call callback to remove session\n");
        goto end;
    }


    if (SSL_get_session(clientssl2) != sess1) {
        printf("Unexpected session found\n");
        goto end;
    }

    if (fix.use_int_cache) {
        if (!SSL_CTX_add_session(cctx, sess2)) {
            /*
             * Should have succeeded because it should not already be in the cache
             */
            printf("Unexpected failure adding session to cache\n");
            goto end;
        }

        if (!SSL_CTX_remove_session(cctx, sess2)) {
            printf("Unexpected failure removing session from cache\n");
            goto end;
        }

        /* This is for the purposes of internal cache testing...ignore the
         * counter for external cache
         */
        if (fix.use_ext_cache)
            remove_called--;
    }

    /* This shouldn't be in the cache so should fail */
    if (SSL_CTX_remove_session(cctx, sess2)) {
        printf("Unexpected success removing session from cache\n");
        goto end;
    }

    if (fix.use_ext_cache && (new_called != 2 || remove_called != 2)) {
        printf("Failed to call callback to remove session #2\n");
        goto end;
    }

#if !defined(OPENSSL_NO_TLS1_1) && !defined(OPENSSL_NO_TLS1_2)
    /* Force a connection failure */
    SSL_CTX_set_max_proto_version(sctx, TLS1_1_VERSION);
    clientssl3 = SSL_new(cctx);
    if (clientssl3 == NULL) {
        printf("Malloc failure\n");
        goto end;
    }
    if (!SSL_set_session(clientssl3, sess1)) {
        printf("Unable to set session for third connection\n");
        goto end;
    }

    /* This should fail because of the mismatched protocol versions */
    if (create_ssl_connection(sctx, cctx, &serverssl3, &clientssl3, NULL,
                               NULL)) {
        printf("Unexpected success creating SSL connection\n");
        goto end;
    }

    /* We should have automatically removed the session from the cache */
    if (fix.use_ext_cache && (new_called != 2 || remove_called != 3)) {
        printf("Failed to call callback to remove session #2\n");
        goto end;
    }

    if (fix.use_int_cache && !SSL_CTX_add_session(cctx, sess2)) {
        /*
         * Should have succeeded because it should not already be in the cache
         */
        printf("Unexpected failure adding session to cache #2\n");
        goto end;
    }
#endif

    testresult = 1;

 end:
    SSL_free(serverssl1);
    SSL_free(clientssl1);
    SSL_free(serverssl2);
    SSL_free(clientssl2);
    SSL_free(serverssl3);
    SSL_free(clientssl3);
    SSL_SESSION_free(sess1);
    SSL_SESSION_free(sess2);
    /*
     * Check if we need to remove any sessions up-refed for the external cache
     */
    if (new_called >= 1)
        SSL_SESSION_free(sess1);
    if (new_called >= 2)
        SSL_SESSION_free(sess2);
    SSL_CTX_free(sctx);
    SSL_CTX_free(cctx);

    return testresult;
}

static int test_session_with_only_int_cache(void) {
    SETUP_TEST_FIXTURE(SSL_SESSION_TEST_FIXTURE, ssl_session_set_up);

    fixture.use_ext_cache = 0;

    EXECUTE_TEST(execute_test_session, ssl_session_tear_down);
}

static int test_session_with_only_ext_cache(void) {
    SETUP_TEST_FIXTURE(SSL_SESSION_TEST_FIXTURE, ssl_session_set_up);

    fixture.use_int_cache = 0;

    EXECUTE_TEST(execute_test_session, ssl_session_tear_down);
}

static int test_session_with_both_cache(void) {
    SETUP_TEST_FIXTURE(SSL_SESSION_TEST_FIXTURE, ssl_session_set_up);

    EXECUTE_TEST(execute_test_session, ssl_session_tear_down);
}

int main(int argc, char *argv[])
{
    BIO *err = NULL;
    int testresult = 1;

    if (argc != 3) {
        printf("Invalid argument count\n");
        return 1;
    }

    cert = argv[1];
    privkey = argv[2];

    err = BIO_new_fp(stderr, BIO_NOCLOSE | BIO_FP_TEXT);

    CRYPTO_set_mem_debug(1);
    CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_ON);

    ADD_TEST(test_tlsext_status_type);
    ADD_TEST(test_session_with_only_int_cache);
    ADD_TEST(test_session_with_only_ext_cache);
    ADD_TEST(test_session_with_both_cache);

    testresult = run_tests(argv[0]);

#ifndef OPENSSL_NO_CRYPTO_MDEBUG
    if (CRYPTO_mem_leaks(err) <= 0)
        testresult = 1;
#endif
    BIO_free(err);

    if (!testresult)
        printf("PASS\n");

    return testresult;
}
