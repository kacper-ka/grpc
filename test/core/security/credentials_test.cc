/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/credentials.h"

#include <stdlib.h>
#include <string.h>

#include <string>

#include <openssl/rsa.h>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"

#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/security/credentials/composite/composite_credentials.h"
#include "src/core/lib/security/credentials/external/aws_external_account_credentials.h"
#include "src/core/lib/security/credentials/external/external_account_credentials.h"
#include "src/core/lib/security/credentials/external/file_external_account_credentials.h"
#include "src/core/lib/security/credentials/external/url_external_account_credentials.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/security/credentials/google_default/google_default_credentials.h"
#include "src/core/lib/security/credentials/jwt/jwt_credentials.h"
#include "src/core/lib/security/credentials/oauth2/oauth2_credentials.h"
#include "src/core/lib/security/transport/auth_filters.h"
#include "src/core/lib/uri/uri_parser.h"
#include "test/core/util/test_config.h"

using grpc_core::internal::grpc_flush_cached_google_default_credentials;
using grpc_core::internal::set_gce_tenancy_checker_for_testing;

/* -- Constants. -- */

static const char test_google_iam_authorization_token[] = "blahblahblhahb";
static const char test_google_iam_authority_selector[] = "respectmyauthoritah";
static const char test_oauth2_bearer_token[] =
    "Bearer blaaslkdjfaslkdfasdsfasf";

/* This JSON key was generated with the GCE console and revoked immediately.
   The identifiers have been changed as well.
   Maximum size for a string literal is 509 chars in C89, yay!  */
static const char test_json_key_str_part1[] =
    "{ \"private_key\": \"-----BEGIN PRIVATE KEY-----"
    "\\nMIICeAIBADANBgkqhkiG9w0BAQEFAASCAmIwggJeAgEAAoGBAOEvJsnoHnyHkXcp\\n7mJE"
    "qg"
    "WGjiw71NfXByguekSKho65FxaGbsnSM9SMQAqVk7Q2rG+I0OpsT0LrWQtZ\\nyjSeg/"
    "rWBQvS4hle4LfijkP3J5BG+"
    "IXDMP8RfziNRQsenAXDNPkY4kJCvKux2xdD\\nOnVF6N7dL3nTYZg+"
    "uQrNsMTz9UxVAgMBAAECgYEAzbLewe1xe9vy+2GoSsfib+28\\nDZgSE6Bu/"
    "zuFoPrRc6qL9p2SsnV7txrunTyJkkOnPLND9ABAXybRTlcVKP/sGgza\\n/"
    "8HpCqFYM9V8f34SBWfD4fRFT+n/"
    "73cfRUtGXdXpseva2lh8RilIQfPhNZAncenU\\ngqXjDvpkypEusgXAykECQQD+";
static const char test_json_key_str_part2[] =
    "53XxNVnxBHsYb+AYEfklR96yVi8HywjVHP34+OQZ\\nCslxoHQM8s+"
    "dBnjfScLu22JqkPv04xyxmt0QAKm9+vTdAkEA4ib7YvEAn2jXzcCI\\nEkoy2L/"
    "XydR1GCHoacdfdAwiL2npOdnbvi4ZmdYRPY1LSTO058tQHKVXV7NLeCa3\\nAARh2QJBAMKeDA"
    "G"
    "W303SQv2cZTdbeaLKJbB5drz3eo3j7dDKjrTD9JupixFbzcGw\\n8FZi5c8idxiwC36kbAL6Hz"
    "A"
    "ZoX+ofI0CQE6KCzPJTtYNqyShgKAZdJ8hwOcvCZtf\\n6z8RJm0+"
    "6YBd38lfh5j8mZd7aHFf6I17j5AQY7oPEc47TjJj/"
    "5nZ68ECQQDvYuI3\\nLyK5fS8g0SYbmPOL9TlcHDOqwG0mrX9qpg5DC2fniXNSrrZ64GTDKdzZ"
    "Y"
    "Ap6LI9W\\nIqv4vr6y38N79TTC\\n-----END PRIVATE KEY-----\\n\", ";
static const char test_json_key_str_part3[] =
    "\"private_key_id\": \"e6b5137873db8d2ef81e06a47289e6434ec8a165\", "
    "\"client_email\": "
    "\"777-abaslkan11hlb6nmim3bpspl31ud@developer.gserviceaccount."
    "com\", \"client_id\": "
    "\"777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent."
    "com\", \"type\": \"service_account\" }";

/* Test refresh token. */
static const char test_refresh_token_str[] =
    "{ \"client_id\": \"32555999999.apps.googleusercontent.com\","
    "  \"client_secret\": \"EmssLNjJy1332hD4KFsecret\","
    "  \"refresh_token\": \"1/Blahblasj424jladJDSGNf-u4Sua3HDA2ngjd42\","
    "  \"type\": \"authorized_user\"}";

/* Test external account credentials. */
static const char test_external_account_credentials_str[] =
    "{\"type\":\"external_account\",\"audience\":\"audience\",\"subject_"
    "token_type\":\"subject_token_type\",\"service_account_impersonation_"
    "url\":\"\",\"token_url\":\"https://"
    "sts.googleapis.com:5555/"
    "token\",\"token_info_url\":\"\",\"credential_source\":{\"file\":"
    "\"credentials_file_path\"},"
    "\"quota_project_id\":\"quota_"
    "project_id\",\"client_id\":\"client_id\",\"client_secret\":\"client_"
    "secret\"}";

static const char test_external_account_credentials_multi_pattern_sts_str[] =
    "{\"type\":\"external_account\",\"audience\":\"audience\",\"subject_"
    "token_type\":\"subject_token_type\",\"service_account_impersonation_"
    "url\":\"https://sts.test.googleapis.com:5555/"
    "service_account_impersonation_url\",\"token_url\":\"https://"
    "test.sts.googleapis.com:5555/token\",\"token_info_url\":\"https://"
    "test-sts.googleapis.com:5555/"
    "token_info\",\"credential_source\":{\"file\":\"credentials_file_path\"},"
    "\"quota_project_id\":\"quota_"
    "project_id\",\"client_id\":\"client_id\",\"client_secret\":\"client_"
    "secret\"}";

static const char test_external_account_credentials_multi_pattern_iam_str[] =
    "{\"type\":\"external_account\",\"audience\":\"audience\",\"subject_"
    "token_type\":\"subject_token_type\",\"service_account_impersonation_"
    "url\":\"https://iamcredentials.test.googleapis.com:5555/"
    "service_account_impersonation_url\",\"token_url\":\"https://"
    "test.iamcredentials.googleapis.com:5555/"
    "token\",\"token_info_url\":\"https://"
    "test-iamcredentials.googleapis.com:5555/"
    "token_info\",\"credential_source\":{\"file\":\"credentials_file_path\"},"
    "\"quota_project_id\":\"quota_"
    "project_id\",\"client_id\":\"client_id\",\"client_secret\":\"client_"
    "secret\"}";

static const char valid_oauth2_json_response[] =
    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
    " \"expires_in\":3599, "
    " \"token_type\":\"Bearer\"}";

static const char valid_sts_json_response[] =
    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
    " \"expires_in\":3599, "
    " \"issued_token_type\":\"urn:ietf:params:oauth:token-type:access_token\", "
    " \"token_type\":\"Bearer\"}";

static const char test_scope[] = "perm1 perm2";

static const char test_signed_jwt[] =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6ImY0OTRkN2M1YWU2MGRmOTcyNmM4YW"
    "U0MDcyZTViYTdmZDkwODg2YzcifQ";
static const char test_signed_jwt_token_type[] =
    "urn:ietf:params:oauth:token-type:id_token";
static const char test_signed_jwt2[] =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6ImY0OTRkN2M1YWU2MGRmOTcyNmM5YW"
    "U2MDcyZTViYTdnZDkwODg5YzcifQ";
static const char test_signed_jwt_token_type2[] =
    "urn:ietf:params:oauth:token-type:jwt";
static const char test_signed_jwt_path_prefix[] = "test_sign_jwt";

static const char test_service_url[] = "https://foo.com/foo.v1";
static const char test_service_url_no_service_name[] = "https://foo.com/";
static const char other_test_service_url[] = "https://bar.com/bar.v1";
static const char other_test_service_url_no_service_name[] = "https://bar.com/";

static const char test_sts_endpoint_url[] =
    "https://foo.com:5555/v1/token-exchange";

static const char test_method[] = "ThisIsNotAMethod";

static const char valid_external_account_creds_token_exchange_response[] =
    "{\"access_token\":\"token_exchange_access_token\","
    " \"expires_in\":3599,"
    " \"token_type\":\"Bearer\"}";

static const char
    valid_external_account_creds_service_account_impersonation_response[] =
        "{\"accessToken\":\"service_account_impersonation_access_token\","
        " \"expireTime\":\"2050-01-01T00:00:00Z\"}";

static const char
    valid_url_external_account_creds_options_credential_source_format_text[] =
        "{\"url\":\"https://foo.com:5555/generate_subject_token_format_text\","
        "\"headers\":{\"Metadata-Flavor\":\"Google\"}}";

static const char
    valid_url_external_account_creds_options_credential_source_with_qurey_params_format_text
        [] = "{\"url\":\"https://foo.com:5555/"
             "path/to/url/creds?p1=v1&p2=v2\","
             "\"headers\":{\"Metadata-Flavor\":\"Google\"}}";

static const char
    valid_url_external_account_creds_retrieve_subject_token_response_format_text
        [] = "test_subject_token";

static const char
    valid_url_external_account_creds_options_credential_source_format_json[] =
        "{\"url\":\"https://foo.com:5555/generate_subject_token_format_json\","
        "\"headers\":{\"Metadata-Flavor\":\"Google\"},"
        "\"format\":{\"type\":\"json\",\"subject_token_field_name\":\"access_"
        "token\"}}";

static const char
    valid_url_external_account_creds_retrieve_subject_token_response_format_json
        [] = "{\"access_token\":\"test_subject_token\"}";

static const char
    invalid_url_external_account_creds_options_credential_source[] =
        "{\"url\":\"invalid_credential_source_url\","
        "\"headers\":{\"Metadata-Flavor\":\"Google\"}}";

static const char
    valid_aws_external_account_creds_retrieve_signing_keys_response[] =
        "{\"AccessKeyId\":\"test_access_key_id\",\"SecretAccessKey\":"
        "\"test_secret_access_key\",\"Token\":\"test_token\"}";

static const char valid_aws_external_account_creds_options_credential_source[] =
    "{\"environment_id\":\"aws1\","
    "\"region_url\":\"https://foo.com:5555/region_url\","
    "\"url\":\"https://foo.com:5555/url\","
    "\"regional_cred_verification_url\":\"https://foo.com:5555/"
    "regional_cred_verification_url_{region}\"}";

static const char
    invalid_aws_external_account_creds_options_credential_source_unmatched_environment_id
        [] = "{\"environment_id\":\"unsupported_aws_version\","
             "\"region_url\":\"https://foo.com:5555/region_url\","
             "\"url\":\"https://foo.com:5555/url\","
             "\"regional_cred_verification_url\":\"https://foo.com:5555/"
             "regional_cred_verification_url_{region}\"}";

static const char
    invalid_aws_external_account_creds_options_credential_source_invalid_region_url
        [] = "{\"environment_id\":\"aws1\","
             "\"region_url\":\"invalid_region_url\","
             "\"url\":\"https://foo.com:5555/url\","
             "\"regional_cred_verification_url\":\"https://foo.com:5555/"
             "regional_cred_verification_url_{region}\"}";

static const char
    invalid_aws_external_account_creds_options_credential_source_invalid_url[] =
        "{\"environment_id\":\"aws1\","
        "\"region_url\":\"https://foo.com:5555/region_url\","
        "\"url\":\"invalid_url\","
        "\"regional_cred_verification_url\":\"https://foo.com:5555/"
        "regional_cred_verification_url_{region}\"}";

static const char
    invalid_aws_external_account_creds_options_credential_source_missing_role_name
        [] = "{\"environment_id\":\"aws1\","
             "\"region_url\":\"https://foo.com:5555/region_url\","
             "\"url\":\"https://foo.com:5555/url_no_role_name\","
             "\"regional_cred_verification_url\":\"https://foo.com:5555/"
             "regional_cred_verification_url_{region}\"}";

static const char
    invalid_aws_external_account_creds_options_credential_source_invalid_regional_cred_verification_url
        [] = "{\"environment_id\":\"aws1\","
             "\"region_url\":\"https://foo.com:5555/region_url\","
             "\"url\":\"https://foo.com:5555/url_no_role_name\","
             "\"regional_cred_verification_url\":\"invalid_regional_cred_"
             "verification_url\"}";

/*  -- Global state flags. -- */

static bool g_test_is_on_gce = false;

static bool g_test_gce_tenancy_checker_called = false;

/* -- Utils. -- */

static char* test_json_key_str(void) {
  size_t result_len = strlen(test_json_key_str_part1) +
                      strlen(test_json_key_str_part2) +
                      strlen(test_json_key_str_part3);
  char* result = static_cast<char*>(gpr_malloc(result_len + 1));
  char* current = result;
  strcpy(result, test_json_key_str_part1);
  current += strlen(test_json_key_str_part1);
  strcpy(current, test_json_key_str_part2);
  current += strlen(test_json_key_str_part2);
  strcpy(current, test_json_key_str_part3);
  return result;
}

static grpc_http_response http_response(int status, const char* body) {
  grpc_http_response response;
  response = {};
  response.status = status;
  response.body = gpr_strdup(const_cast<char*>(body));
  response.body_length = strlen(body);
  return response;
}

/* -- Tests. -- */

static void test_oauth2_token_fetcher_creds_parsing_ok(void) {
  grpc_core::ExecCtx exec_ctx;
  absl::optional<grpc_core::Slice> token_value;
  grpc_millis token_lifetime;
  grpc_http_response response = http_response(200, valid_oauth2_json_response);
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &response, &token_value, &token_lifetime) ==
             GRPC_CREDENTIALS_OK);
  GPR_ASSERT(token_lifetime == 3599 * GPR_MS_PER_SEC);
  GPR_ASSERT(token_value->as_string_view() ==
             "Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_");
  grpc_http_response_destroy(&response);
}

static void test_oauth2_token_fetcher_creds_parsing_bad_http_status(void) {
  grpc_core::ExecCtx exec_ctx;
  absl::optional<grpc_core::Slice> token_value;
  grpc_millis token_lifetime;
  grpc_http_response response = http_response(401, valid_oauth2_json_response);
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &response, &token_value, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
  grpc_http_response_destroy(&response);
}

static void test_oauth2_token_fetcher_creds_parsing_empty_http_body(void) {
  grpc_core::ExecCtx exec_ctx;
  absl::optional<grpc_core::Slice> token_value;
  grpc_millis token_lifetime;
  grpc_http_response response = http_response(200, "");
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &response, &token_value, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
  grpc_http_response_destroy(&response);
}

static void test_oauth2_token_fetcher_creds_parsing_invalid_json(void) {
  grpc_core::ExecCtx exec_ctx;
  absl::optional<grpc_core::Slice> token_value;
  grpc_millis token_lifetime;
  grpc_http_response response =
      http_response(200,
                    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
                    " \"expires_in\":3599, "
                    " \"token_type\":\"Bearer\"");
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &response, &token_value, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
  grpc_http_response_destroy(&response);
}

static void test_oauth2_token_fetcher_creds_parsing_missing_token(void) {
  grpc_core::ExecCtx exec_ctx;
  absl::optional<grpc_core::Slice> token_value;
  grpc_millis token_lifetime;
  grpc_http_response response = http_response(200,
                                              "{"
                                              " \"expires_in\":3599, "
                                              " \"token_type\":\"Bearer\"}");
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &response, &token_value, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
  grpc_http_response_destroy(&response);
}

static void test_oauth2_token_fetcher_creds_parsing_missing_token_type(void) {
  grpc_core::ExecCtx exec_ctx;
  absl::optional<grpc_core::Slice> token_value;
  grpc_millis token_lifetime;
  grpc_http_response response =
      http_response(200,
                    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
                    " \"expires_in\":3599, "
                    "}");
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &response, &token_value, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
  grpc_http_response_destroy(&response);
}

static void test_oauth2_token_fetcher_creds_parsing_missing_token_lifetime(
    void) {
  grpc_core::ExecCtx exec_ctx;
  absl::optional<grpc_core::Slice> token_value;
  grpc_millis token_lifetime;
  grpc_http_response response =
      http_response(200,
                    "{\"access_token\":\"ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_\","
                    " \"token_type\":\"Bearer\"}");
  GPR_ASSERT(grpc_oauth2_token_fetcher_credentials_parse_server_response(
                 &response, &token_value, &token_lifetime) ==
             GRPC_CREDENTIALS_ERROR);
  grpc_http_response_destroy(&response);
}

namespace {

class RequestMetadataState {
 public:
  static RequestMetadataState* NewInstance(
      grpc_error_handle expected_error,
      std::map<std::string, std::string> expected) {
    RequestMetadataState* state = new RequestMetadataState(
        expected_error, std::move(expected),
        grpc_polling_entity_create_from_pollset_set(grpc_pollset_set_create()));
    return state;
  }

 private:
  RequestMetadataState(grpc_error_handle expected_error,
                       std::map<std::string, std::string> expected,
                       grpc_polling_entity pollent)
      : expected_error_(expected_error),
        expected_(expected),
        pollent_(pollent) {
    GRPC_CLOSURE_INIT(&on_request_metadata_, OnRequestMetadata, this,
                      grpc_schedule_on_exec_ctx);
  }

 public:
  ~RequestMetadataState() {
    grpc_pollset_set_destroy(grpc_polling_entity_pollset_set(&pollent_));
  }

  void RunRequestMetadataTest(grpc_call_credentials* creds,
                              grpc_auth_metadata_context auth_md_ctx) {
    grpc_error_handle error = GRPC_ERROR_NONE;
    if (creds->get_request_metadata(&pollent_, auth_md_ctx, &md_array_,
                                    &on_request_metadata_, &error)) {
      // Synchronous result.  Invoke the callback directly.
      CheckRequestMetadata(error);
      GRPC_ERROR_UNREF(error);
    }
  }

 private:
  static void OnRequestMetadata(void* arg, grpc_error_handle error) {
    RequestMetadataState* state = static_cast<RequestMetadataState*>(arg);
    state->CheckRequestMetadata(error);
  }

  void CheckRequestMetadata(grpc_error_handle error) {
    gpr_log(GPR_INFO, "expected_error: %s",
            grpc_error_std_string(expected_error_).c_str());
    gpr_log(GPR_INFO, "actual_error: %s", grpc_error_std_string(error).c_str());
    if (expected_error_ == GRPC_ERROR_NONE) {
      GPR_ASSERT(error == GRPC_ERROR_NONE);
    } else {
      std::string expected_error;
      GPR_ASSERT(grpc_error_get_str(expected_error_, GRPC_ERROR_STR_DESCRIPTION,
                                    &expected_error));
      std::string actual_error;
      GPR_ASSERT(
          grpc_error_get_str(error, GRPC_ERROR_STR_DESCRIPTION, &actual_error));
      GPR_ASSERT(expected_error == actual_error);
      GRPC_ERROR_UNREF(expected_error_);
    }
    gpr_log(GPR_INFO, "expected_size=%" PRIdPTR " actual_size=%" PRIdPTR,
            expected_.size(), md_array_.size());
    GPR_ASSERT(md_array_.size() == expected_.size());
    CheckMetadata(expected_, &md_array_);
    delete this;
  }

  static void CheckMetadata(const std::map<std::string, std::string>& expected,
                            grpc_core::CredentialsMetadataArray* md_array) {
    for (auto const& i : expected) {
      size_t j;
      for (j = 0; j < md_array->size(); ++j) {
        absl::string_view actual_key = md_array->at(j).first.as_string_view();
        if (actual_key == i.first) {
          absl::string_view actual_value =
              md_array->at(j).second.as_string_view();
          GPR_ASSERT(actual_value == i.second);
          break;
        }
      }
      if (j == md_array->size()) {
        gpr_log(GPR_ERROR, "key %s not found", i.first.c_str());
        GPR_ASSERT(0);
      }
    }
  }

 private:
  grpc_error_handle expected_error_;
  std::map<std::string, std::string> expected_;
  grpc_core::CredentialsMetadataArray md_array_;
  grpc_closure on_request_metadata_;
  grpc_polling_entity pollent_;
};

}  // namespace

static void test_google_iam_creds(void) {
  grpc_core::ExecCtx exec_ctx;
  std::map<std::string, std::string> emd = {
      {GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY,
       test_google_iam_authorization_token},
      {GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY,
       test_google_iam_authority_selector}};
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_call_credentials* creds = grpc_google_iam_credentials_create(
      test_google_iam_authorization_token, test_google_iam_authority_selector,
      nullptr);
  /* Check security level. */
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  state->RunRequestMetadataTest(creds, auth_md_ctx);
  creds->Unref();
}

static void test_access_token_creds(void) {
  grpc_core::ExecCtx exec_ctx;
  std::map<std::string, std::string> emd = {
      {GRPC_AUTHORIZATION_METADATA_KEY, "Bearer blah"}};
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_call_credentials* creds =
      grpc_access_token_credentials_create("blah", nullptr);
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  GPR_ASSERT(strcmp(creds->type(), GRPC_CALL_CREDENTIALS_TYPE_OAUTH2) == 0);
  /* Check security level. */
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  state->RunRequestMetadataTest(creds, auth_md_ctx);
  creds->Unref();
}

namespace {
class check_channel_oauth2 final : public grpc_channel_credentials {
 public:
  check_channel_oauth2() : grpc_channel_credentials("mock") {}
  ~check_channel_oauth2() override = default;

  grpc_core::RefCountedPtr<grpc_channel_security_connector>
  create_security_connector(
      grpc_core::RefCountedPtr<grpc_call_credentials> call_creds,
      const char* /*target*/, const grpc_channel_args* /*args*/,
      grpc_channel_args** /*new_args*/) override {
    GPR_ASSERT(strcmp(type(), "mock") == 0);
    GPR_ASSERT(call_creds != nullptr);
    GPR_ASSERT(strcmp(call_creds->type(), GRPC_CALL_CREDENTIALS_TYPE_OAUTH2) ==
               0);
    return nullptr;
  }
};
}  // namespace

static void test_channel_oauth2_composite_creds(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_channel_args* new_args;
  grpc_channel_credentials* channel_creds = new check_channel_oauth2();
  grpc_call_credentials* oauth2_creds =
      grpc_access_token_credentials_create("blah", nullptr);
  grpc_channel_credentials* channel_oauth2_creds =
      grpc_composite_channel_credentials_create(channel_creds, oauth2_creds,
                                                nullptr);
  grpc_channel_credentials_release(channel_creds);
  grpc_call_credentials_release(oauth2_creds);
  channel_oauth2_creds->create_security_connector(nullptr, nullptr, nullptr,
                                                  &new_args);
  grpc_channel_credentials_release(channel_oauth2_creds);
}

static void test_oauth2_google_iam_composite_creds(void) {
  grpc_core::ExecCtx exec_ctx;
  std::map<std::string, std::string> emd = {
      {GRPC_AUTHORIZATION_METADATA_KEY, test_oauth2_bearer_token},
      {GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY,
       test_google_iam_authorization_token},
      {GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY,
       test_google_iam_authority_selector}};
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_call_credentials* oauth2_creds = grpc_md_only_test_credentials_create(
      "authorization", test_oauth2_bearer_token, false);

  /* Check security level of fake credentials. */
  GPR_ASSERT(oauth2_creds->min_security_level() == GRPC_SECURITY_NONE);

  grpc_call_credentials* google_iam_creds = grpc_google_iam_credentials_create(
      test_google_iam_authorization_token, test_google_iam_authority_selector,
      nullptr);
  grpc_call_credentials* composite_creds =
      grpc_composite_call_credentials_create(oauth2_creds, google_iam_creds,
                                             nullptr);
  /* Check security level of composite credentials. */
  GPR_ASSERT(composite_creds->min_security_level() ==
             GRPC_PRIVACY_AND_INTEGRITY);

  oauth2_creds->Unref();
  google_iam_creds->Unref();
  GPR_ASSERT(strcmp(composite_creds->type(),
                    GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE) == 0);
  const grpc_composite_call_credentials::CallCredentialsList& creds_list =
      static_cast<const grpc_composite_call_credentials*>(composite_creds)
          ->inner();
  GPR_ASSERT(creds_list.size() == 2);
  GPR_ASSERT(strcmp(creds_list[0]->type(), GRPC_CALL_CREDENTIALS_TYPE_OAUTH2) ==
             0);
  GPR_ASSERT(strcmp(creds_list[1]->type(), GRPC_CALL_CREDENTIALS_TYPE_IAM) ==
             0);
  state->RunRequestMetadataTest(composite_creds, auth_md_ctx);
  composite_creds->Unref();
}

namespace {
class check_channel_oauth2_google_iam final : public grpc_channel_credentials {
 public:
  check_channel_oauth2_google_iam() : grpc_channel_credentials("mock") {}
  ~check_channel_oauth2_google_iam() override = default;

  grpc_core::RefCountedPtr<grpc_channel_security_connector>
  create_security_connector(
      grpc_core::RefCountedPtr<grpc_call_credentials> call_creds,
      const char* /*target*/, const grpc_channel_args* /*args*/,
      grpc_channel_args** /*new_args*/) override {
    GPR_ASSERT(strcmp(type(), "mock") == 0);
    GPR_ASSERT(call_creds != nullptr);
    GPR_ASSERT(
        strcmp(call_creds->type(), GRPC_CALL_CREDENTIALS_TYPE_COMPOSITE) == 0);
    const grpc_composite_call_credentials::CallCredentialsList& creds_list =
        static_cast<const grpc_composite_call_credentials*>(call_creds.get())
            ->inner();
    GPR_ASSERT(
        strcmp(creds_list[0]->type(), GRPC_CALL_CREDENTIALS_TYPE_OAUTH2) == 0);
    GPR_ASSERT(strcmp(creds_list[1]->type(), GRPC_CALL_CREDENTIALS_TYPE_IAM) ==
               0);
    return nullptr;
  }
};
}  // namespace

static void test_channel_oauth2_google_iam_composite_creds(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_channel_args* new_args;
  grpc_channel_credentials* channel_creds =
      new check_channel_oauth2_google_iam();
  grpc_call_credentials* oauth2_creds =
      grpc_access_token_credentials_create("blah", nullptr);
  grpc_channel_credentials* channel_oauth2_creds =
      grpc_composite_channel_credentials_create(channel_creds, oauth2_creds,
                                                nullptr);
  grpc_call_credentials* google_iam_creds = grpc_google_iam_credentials_create(
      test_google_iam_authorization_token, test_google_iam_authority_selector,
      nullptr);

  grpc_channel_credentials* channel_oauth2_iam_creds =
      grpc_composite_channel_credentials_create(channel_oauth2_creds,
                                                google_iam_creds, nullptr);
  grpc_channel_credentials_release(channel_creds);
  grpc_call_credentials_release(oauth2_creds);
  grpc_channel_credentials_release(channel_oauth2_creds);
  grpc_call_credentials_release(google_iam_creds);

  channel_oauth2_iam_creds->create_security_connector(nullptr, nullptr, nullptr,
                                                      &new_args);

  grpc_channel_credentials_release(channel_oauth2_iam_creds);
}

static void validate_compute_engine_http_request(
    const grpc_http_request* request, const char* host, const char* path) {
  GPR_ASSERT(strcmp(host, "metadata.google.internal.") == 0);
  GPR_ASSERT(
      strcmp(path,
             "/computeMetadata/v1/instance/service-accounts/default/token") ==
      0);
  GPR_ASSERT(request->hdr_count == 1);
  GPR_ASSERT(strcmp(request->hdrs[0].key, "Metadata-Flavor") == 0);
  GPR_ASSERT(strcmp(request->hdrs[0].value, "Google") == 0);
}

static int compute_engine_httpcli_get_success_override(
    const grpc_http_request* request, const char* host, const char* path,
    grpc_millis /*deadline*/, grpc_closure* on_done,
    grpc_http_response* response) {
  validate_compute_engine_http_request(request, host, path);
  *response = http_response(200, valid_oauth2_json_response);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

static int compute_engine_httpcli_get_failure_override(
    const grpc_http_request* request, const char* host, const char* path,
    grpc_millis /*deadline*/, grpc_closure* on_done,
    grpc_http_response* response) {
  validate_compute_engine_http_request(request, host, path);
  *response = http_response(403, "Not Authorized.");
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

static int httpcli_post_should_not_be_called(
    const grpc_http_request* /*request*/, const char* /*host*/,
    const char* /*path*/, const char* /*body_bytes*/, size_t /*body_size*/,
    grpc_millis /*deadline*/, grpc_closure* /*on_done*/,
    grpc_http_response* /*response*/) {
  GPR_ASSERT("HTTP POST should not be called" == nullptr);
  return 1;
}

static int httpcli_get_should_not_be_called(
    const grpc_http_request* /*request*/, const char* /*host*/,
    const char* /*path*/, grpc_millis /*deadline*/, grpc_closure* /*on_done*/,
    grpc_http_response* /*response*/) {
  GPR_ASSERT("HTTP GET should not be called" == nullptr);
  return 1;
}

static void test_compute_engine_creds_success() {
  grpc_core::ExecCtx exec_ctx;
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_"}};
  const char expected_creds_debug_string[] =
      "GoogleComputeEngineTokenFetcherCredentials{"
      "OAuth2TokenFetcherCredentials}";
  grpc_call_credentials* creds =
      grpc_google_compute_engine_credentials_create(nullptr);
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  /* Check security level. */
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);

  /* First request: http get should be called. */
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_core::HttpRequest::SetOverride(
      compute_engine_httpcli_get_success_override,
      httpcli_post_should_not_be_called);
  state->RunRequestMetadataTest(creds, auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();

  /* Second request: the cached token should be served directly. */
  state = RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_core::HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                                      httpcli_post_should_not_be_called);
  state->RunRequestMetadataTest(creds, auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();

  GPR_ASSERT(
      strcmp(creds->debug_string().c_str(), expected_creds_debug_string) == 0);
  creds->Unref();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
}

static void test_compute_engine_creds_failure(void) {
  grpc_core::ExecCtx exec_ctx;
  const char expected_creds_debug_string[] =
      "GoogleComputeEngineTokenFetcherCredentials{"
      "OAuth2TokenFetcherCredentials}";
  RequestMetadataState* state = RequestMetadataState::NewInstance(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Error occurred when fetching oauth2 token."),
      {});
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_call_credentials* creds =
      grpc_google_compute_engine_credentials_create(nullptr);
  grpc_core::HttpRequest::SetOverride(
      compute_engine_httpcli_get_failure_override,
      httpcli_post_should_not_be_called);
  state->RunRequestMetadataTest(creds, auth_md_ctx);
  GPR_ASSERT(
      strcmp(creds->debug_string().c_str(), expected_creds_debug_string) == 0);
  creds->Unref();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
}

static void validate_refresh_token_http_request(
    const grpc_http_request* request, const char* host, const char* path,
    const char* body, size_t body_size) {
  /* The content of the assertion is tested extensively in json_token_test. */
  GPR_ASSERT(body != nullptr);
  GPR_ASSERT(body_size != 0);
  std::string expected_body = absl::StrFormat(
      GRPC_REFRESH_TOKEN_POST_BODY_FORMAT_STRING,
      "32555999999.apps.googleusercontent.com", "EmssLNjJy1332hD4KFsecret",
      "1/Blahblasj424jladJDSGNf-u4Sua3HDA2ngjd42");
  GPR_ASSERT(expected_body.size() == body_size);
  GPR_ASSERT(memcmp(expected_body.data(), body, body_size) == 0);
  GPR_ASSERT(strcmp(host, GRPC_GOOGLE_OAUTH2_SERVICE_HOST) == 0);
  GPR_ASSERT(strcmp(path, GRPC_GOOGLE_OAUTH2_SERVICE_TOKEN_PATH) == 0);
  GPR_ASSERT(request->hdr_count == 1);
  GPR_ASSERT(strcmp(request->hdrs[0].key, "Content-Type") == 0);
  GPR_ASSERT(
      strcmp(request->hdrs[0].value, "application/x-www-form-urlencoded") == 0);
}

static int refresh_token_httpcli_post_success(
    const grpc_http_request* request, const char* host, const char* path,
    const char* body, size_t body_size, grpc_millis /*deadline*/,
    grpc_closure* on_done, grpc_http_response* response) {
  validate_refresh_token_http_request(request, host, path, body, body_size);
  *response = http_response(200, valid_oauth2_json_response);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

static int token_httpcli_post_failure(
    const grpc_http_request* /*request*/, const char* /*host*/,
    const char* /*path*/, const char* /*body*/, size_t /*body_size*/,
    grpc_millis /*deadline*/, grpc_closure* on_done,
    grpc_http_response* response) {
  *response = http_response(403, "Not Authorized.");
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

static void test_refresh_token_creds_success(void) {
  grpc_core::ExecCtx exec_ctx;
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_"}};
  const char expected_creds_debug_string[] =
      "GoogleRefreshToken{ClientID:32555999999.apps.googleusercontent.com,"
      "OAuth2TokenFetcherCredentials}";
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_call_credentials* creds = grpc_google_refresh_token_credentials_create(
      test_refresh_token_str, nullptr);

  /* Check security level. */
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);

  /* First request: http put should be called. */
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_core::HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                                      refresh_token_httpcli_post_success);
  state->RunRequestMetadataTest(creds, auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();

  /* Second request: the cached token should be served directly. */
  state = RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_core::HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                                      httpcli_post_should_not_be_called);
  state->RunRequestMetadataTest(creds, auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(
      strcmp(creds->debug_string().c_str(), expected_creds_debug_string) == 0);

  creds->Unref();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
}

static void test_refresh_token_creds_failure(void) {
  grpc_core::ExecCtx exec_ctx;
  const char expected_creds_debug_string[] =
      "GoogleRefreshToken{ClientID:32555999999.apps.googleusercontent.com,"
      "OAuth2TokenFetcherCredentials}";
  RequestMetadataState* state = RequestMetadataState::NewInstance(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Error occurred when fetching oauth2 token."),
      {});
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_call_credentials* creds = grpc_google_refresh_token_credentials_create(
      test_refresh_token_str, nullptr);
  grpc_core::HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                                      token_httpcli_post_failure);
  state->RunRequestMetadataTest(creds, auth_md_ctx);
  GPR_ASSERT(
      strcmp(creds->debug_string().c_str(), expected_creds_debug_string) == 0);

  creds->Unref();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
}

static void test_valid_sts_creds_options(void) {
  grpc_sts_credentials_options valid_options = {
      test_sts_endpoint_url,        // sts_endpoint_url
      nullptr,                      // resource
      nullptr,                      // audience
      nullptr,                      // scope
      nullptr,                      // requested_token_type
      test_signed_jwt_path_prefix,  // subject_token_path
      test_signed_jwt_token_type,   // subject_token_type
      nullptr,                      // actor_token_path
      nullptr                       // actor_token_type
  };
  absl::StatusOr<grpc_core::URI> sts_url =
      grpc_core::ValidateStsCredentialsOptions(&valid_options);
  GPR_ASSERT(sts_url.ok());
  absl::string_view host;
  absl::string_view port;
  GPR_ASSERT(grpc_core::SplitHostPort(sts_url->authority(), &host, &port));
  GPR_ASSERT(host == "foo.com");
  GPR_ASSERT(port == "5555");
}

static void test_invalid_sts_creds_options(void) {
  grpc_sts_credentials_options invalid_options = {
      test_sts_endpoint_url,       // sts_endpoint_url
      nullptr,                     // resource
      nullptr,                     // audience
      nullptr,                     // scope
      nullptr,                     // requested_token_type
      nullptr,                     // subject_token_path (Required)
      test_signed_jwt_token_type,  // subject_token_type
      nullptr,                     // actor_token_path
      nullptr                      // actor_token_type
  };
  absl::StatusOr<grpc_core::URI> url_should_be_invalid =
      grpc_core::ValidateStsCredentialsOptions(&invalid_options);
  GPR_ASSERT(!url_should_be_invalid.ok());

  invalid_options = {
      test_sts_endpoint_url,        // sts_endpoint_url
      nullptr,                      // resource
      nullptr,                      // audience
      nullptr,                      // scope
      nullptr,                      // requested_token_type
      test_signed_jwt_path_prefix,  // subject_token_path
      nullptr,                      // subject_token_type (Required)
      nullptr,                      // actor_token_path
      nullptr                       // actor_token_type
  };
  url_should_be_invalid =
      grpc_core::ValidateStsCredentialsOptions(&invalid_options);
  GPR_ASSERT(!url_should_be_invalid.ok());

  invalid_options = {
      nullptr,                      // sts_endpoint_url (Required)
      nullptr,                      // resource
      nullptr,                      // audience
      nullptr,                      // scope
      nullptr,                      // requested_token_type
      test_signed_jwt_path_prefix,  // subject_token_path
      test_signed_jwt_token_type,   // subject_token_type (Required)
      nullptr,                      // actor_token_path
      nullptr                       // actor_token_type
  };
  url_should_be_invalid =
      grpc_core::ValidateStsCredentialsOptions(&invalid_options);
  GPR_ASSERT(!url_should_be_invalid.ok());

  invalid_options = {
      "not_a_valid_uri",            // sts_endpoint_url
      nullptr,                      // resource
      nullptr,                      // audience
      nullptr,                      // scope
      nullptr,                      // requested_token_type
      test_signed_jwt_path_prefix,  // subject_token_path
      test_signed_jwt_token_type,   // subject_token_type (Required)
      nullptr,                      // actor_token_path
      nullptr                       // actor_token_type
  };
  url_should_be_invalid =
      grpc_core::ValidateStsCredentialsOptions(&invalid_options);
  GPR_ASSERT(!url_should_be_invalid.ok());

  invalid_options = {
      "ftp://ftp.is.not.a.valid.scheme/bar",  // sts_endpoint_url
      nullptr,                                // resource
      nullptr,                                // audience
      nullptr,                                // scope
      nullptr,                                // requested_token_type
      test_signed_jwt_path_prefix,            // subject_token_path
      test_signed_jwt_token_type,             // subject_token_type (Required)
      nullptr,                                // actor_token_path
      nullptr                                 // actor_token_type
  };
  url_should_be_invalid =
      grpc_core::ValidateStsCredentialsOptions(&invalid_options);
  GPR_ASSERT(!url_should_be_invalid.ok());
}

static void assert_query_parameters(const grpc_core::URI& uri,
                                    absl::string_view expected_key,
                                    absl::string_view expected_val) {
  const auto it = uri.query_parameter_map().find(expected_key);
  GPR_ASSERT(it != uri.query_parameter_map().end());
  if (it->second != expected_val) {
    gpr_log(GPR_ERROR, "%s!=%s", std::string(it->second).c_str(),
            std::string(expected_val).c_str());
  }
  GPR_ASSERT(it->second == expected_val);
}

static void validate_sts_token_http_request(const grpc_http_request* request,
                                            const char* host, const char* path,
                                            const char* body, size_t body_size,
                                            bool expect_actor_token) {
  // Check that the body is constructed properly.
  GPR_ASSERT(body != nullptr);
  GPR_ASSERT(body_size != 0);
  std::string get_url_equivalent =
      absl::StrFormat("%s?%s", test_sts_endpoint_url, body);
  absl::StatusOr<grpc_core::URI> url =
      grpc_core::URI::Parse(get_url_equivalent);
  if (!url.ok()) {
    gpr_log(GPR_ERROR, "%s", url.status().ToString().c_str());
    GPR_ASSERT(url.ok());
  }
  assert_query_parameters(*url, "resource", "resource");
  assert_query_parameters(*url, "audience", "audience");
  assert_query_parameters(*url, "scope", "scope");
  assert_query_parameters(*url, "requested_token_type", "requested_token_type");
  assert_query_parameters(*url, "subject_token", test_signed_jwt);
  assert_query_parameters(*url, "subject_token_type",
                          test_signed_jwt_token_type);
  if (expect_actor_token) {
    assert_query_parameters(*url, "actor_token", test_signed_jwt2);
    assert_query_parameters(*url, "actor_token_type",
                            test_signed_jwt_token_type2);
  } else {
    GPR_ASSERT(url->query_parameter_map().find("actor_token") ==
               url->query_parameter_map().end());
    GPR_ASSERT(url->query_parameter_map().find("actor_token_type") ==
               url->query_parameter_map().end());
  }

  // Check the rest of the request.
  GPR_ASSERT(strcmp(host, "foo.com:5555") == 0);
  GPR_ASSERT(strcmp(path, "/v1/token-exchange") == 0);
  GPR_ASSERT(request->hdr_count == 1);
  GPR_ASSERT(strcmp(request->hdrs[0].key, "Content-Type") == 0);
  GPR_ASSERT(
      strcmp(request->hdrs[0].value, "application/x-www-form-urlencoded") == 0);
}

static int sts_token_httpcli_post_success(const grpc_http_request* request,
                                          const char* host, const char* path,
                                          const char* body, size_t body_size,
                                          grpc_millis /*deadline*/,
                                          grpc_closure* on_done,
                                          grpc_http_response* response) {
  validate_sts_token_http_request(request, host, path, body, body_size, true);
  *response = http_response(200, valid_sts_json_response);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

static int sts_token_httpcli_post_success_no_actor_token(
    const grpc_http_request* request, const char* host, const char* path,
    const char* body, size_t body_size, grpc_millis /*deadline*/,
    grpc_closure* on_done, grpc_http_response* response) {
  validate_sts_token_http_request(request, host, path, body, body_size, false);
  *response = http_response(200, valid_sts_json_response);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

static char* write_tmp_jwt_file(const char* jwt_contents) {
  char* path;
  FILE* tmp = gpr_tmpfile(test_signed_jwt_path_prefix, &path);
  GPR_ASSERT(path != nullptr);
  GPR_ASSERT(tmp != nullptr);
  size_t jwt_length = strlen(jwt_contents);
  GPR_ASSERT(fwrite(jwt_contents, 1, jwt_length, tmp) == jwt_length);
  fclose(tmp);
  return path;
}

static void test_sts_creds_success(void) {
  grpc_core::ExecCtx exec_ctx;
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_"}};
  const char expected_creds_debug_string[] =
      "StsTokenFetcherCredentials{Path:/v1/"
      "token-exchange,Authority:foo.com:5555,OAuth2TokenFetcherCredentials}";
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  char* subject_token_path = write_tmp_jwt_file(test_signed_jwt);
  char* actor_token_path = write_tmp_jwt_file(test_signed_jwt2);
  grpc_sts_credentials_options valid_options = {
      test_sts_endpoint_url,       // sts_endpoint_url
      "resource",                  // resource
      "audience",                  // audience
      "scope",                     // scope
      "requested_token_type",      // requested_token_type
      subject_token_path,          // subject_token_path
      test_signed_jwt_token_type,  // subject_token_type
      actor_token_path,            // actor_token_path
      test_signed_jwt_token_type2  // actor_token_type
  };
  grpc_call_credentials* creds =
      grpc_sts_credentials_create(&valid_options, nullptr);

  /* Check security level. */
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);

  /* First request: http put should be called. */
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_core::HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                                      sts_token_httpcli_post_success);
  state->RunRequestMetadataTest(creds, auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();

  /* Second request: the cached token should be served directly. */
  state = RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_core::HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                                      httpcli_post_should_not_be_called);
  state->RunRequestMetadataTest(creds, auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(
      strcmp(creds->debug_string().c_str(), expected_creds_debug_string) == 0);

  creds->Unref();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
  gpr_free(subject_token_path);
  gpr_free(actor_token_path);
}

static void test_sts_creds_token_file_not_found(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_sts_credentials_options valid_options = {
      test_sts_endpoint_url,           // sts_endpoint_url
      "resource",                      // resource
      "audience",                      // audience
      "scope",                         // scope
      "requested_token_type",          // requested_token_type
      "/some/completely/random/path",  // subject_token_path
      test_signed_jwt_token_type,      // subject_token_type
      "",                              // actor_token_path
      ""                               // actor_token_type
  };
  grpc_call_credentials* creds =
      grpc_sts_credentials_create(&valid_options, nullptr);

  /* Check security level. */
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);

  RequestMetadataState* state = RequestMetadataState::NewInstance(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Error occurred when fetching oauth2 token."),
      {});
  grpc_core::HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                                      httpcli_post_should_not_be_called);
  state->RunRequestMetadataTest(creds, auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();

  /* Cleanup. */
  creds->Unref();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
}

static void test_sts_creds_no_actor_token_success(void) {
  grpc_core::ExecCtx exec_ctx;
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_"}};
  const char expected_creds_debug_string[] =
      "StsTokenFetcherCredentials{Path:/v1/"
      "token-exchange,Authority:foo.com:5555,OAuth2TokenFetcherCredentials}";
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  char* subject_token_path = write_tmp_jwt_file(test_signed_jwt);
  grpc_sts_credentials_options valid_options = {
      test_sts_endpoint_url,       // sts_endpoint_url
      "resource",                  // resource
      "audience",                  // audience
      "scope",                     // scope
      "requested_token_type",      // requested_token_type
      subject_token_path,          // subject_token_path
      test_signed_jwt_token_type,  // subject_token_type
      "",                          // actor_token_path
      ""                           // actor_token_type
  };
  grpc_call_credentials* creds =
      grpc_sts_credentials_create(&valid_options, nullptr);

  /* Check security level. */
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);

  /* First request: http put should be called. */
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_core::HttpRequest::SetOverride(
      httpcli_get_should_not_be_called,
      sts_token_httpcli_post_success_no_actor_token);
  state->RunRequestMetadataTest(creds, auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();

  /* Second request: the cached token should be served directly. */
  state = RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_core::HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                                      httpcli_post_should_not_be_called);
  state->RunRequestMetadataTest(creds, auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(
      strcmp(creds->debug_string().c_str(), expected_creds_debug_string) == 0);

  creds->Unref();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
  gpr_free(subject_token_path);
}

static void test_sts_creds_load_token_failure(void) {
  const char expected_creds_debug_string[] =
      "StsTokenFetcherCredentials{Path:/v1/"
      "token-exchange,Authority:foo.com:5555,OAuth2TokenFetcherCredentials}";
  grpc_core::ExecCtx exec_ctx;
  RequestMetadataState* state = RequestMetadataState::NewInstance(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Error occurred when fetching oauth2 token."),
      {});
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  char* test_signed_jwt_path = write_tmp_jwt_file(test_signed_jwt);
  grpc_sts_credentials_options options = {
      test_sts_endpoint_url,       // sts_endpoint_url
      "resource",                  // resource
      "audience",                  // audience
      "scope",                     // scope
      "requested_token_type",      // requested_token_type
      "invalid_path",              // subject_token_path
      test_signed_jwt_token_type,  // subject_token_type
      nullptr,                     // actor_token_path
      nullptr                      // actor_token_type
  };
  grpc_call_credentials* creds = grpc_sts_credentials_create(&options, nullptr);
  grpc_core::HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                                      httpcli_post_should_not_be_called);
  state->RunRequestMetadataTest(creds, auth_md_ctx);
  GPR_ASSERT(
      strcmp(creds->debug_string().c_str(), expected_creds_debug_string) == 0);

  creds->Unref();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
  gpr_free(test_signed_jwt_path);
}

static void test_sts_creds_http_failure(void) {
  const char expected_creds_debug_string[] =
      "StsTokenFetcherCredentials{Path:/v1/"
      "token-exchange,Authority:foo.com:5555,OAuth2TokenFetcherCredentials}";
  grpc_core::ExecCtx exec_ctx;
  RequestMetadataState* state = RequestMetadataState::NewInstance(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Error occurred when fetching oauth2 token."),
      {});
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  char* test_signed_jwt_path = write_tmp_jwt_file(test_signed_jwt);
  grpc_sts_credentials_options valid_options = {
      test_sts_endpoint_url,       // sts_endpoint_url
      "resource",                  // resource
      "audience",                  // audience
      "scope",                     // scope
      "requested_token_type",      // requested_token_type
      test_signed_jwt_path,        // subject_token_path
      test_signed_jwt_token_type,  // subject_token_type
      nullptr,                     // actor_token_path
      nullptr                      // actor_token_type
  };
  grpc_call_credentials* creds =
      grpc_sts_credentials_create(&valid_options, nullptr);
  grpc_core::HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                                      token_httpcli_post_failure);
  state->RunRequestMetadataTest(creds, auth_md_ctx);
  GPR_ASSERT(
      strcmp(creds->debug_string().c_str(), expected_creds_debug_string) == 0);
  creds->Unref();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
  gpr_free(test_signed_jwt_path);
}

static void validate_jwt_encode_and_sign_params(
    const grpc_auth_json_key* json_key, const char* scope,
    gpr_timespec token_lifetime) {
  GPR_ASSERT(grpc_auth_json_key_is_valid(json_key));
  GPR_ASSERT(json_key->private_key != nullptr);
  GPR_ASSERT(RSA_check_key(json_key->private_key));
  GPR_ASSERT(json_key->type != nullptr &&
             strcmp(json_key->type, "service_account") == 0);
  GPR_ASSERT(json_key->private_key_id != nullptr &&
             strcmp(json_key->private_key_id,
                    "e6b5137873db8d2ef81e06a47289e6434ec8a165") == 0);
  GPR_ASSERT(json_key->client_id != nullptr &&
             strcmp(json_key->client_id,
                    "777-abaslkan11hlb6nmim3bpspl31ud.apps."
                    "googleusercontent.com") == 0);
  GPR_ASSERT(json_key->client_email != nullptr &&
             strcmp(json_key->client_email,
                    "777-abaslkan11hlb6nmim3bpspl31ud@developer."
                    "gserviceaccount.com") == 0);
  if (scope != nullptr) GPR_ASSERT(strcmp(scope, test_scope) == 0);
  GPR_ASSERT(gpr_time_cmp(token_lifetime, grpc_max_auth_token_lifetime()) == 0);
}

static char* encode_and_sign_jwt_success(const grpc_auth_json_key* json_key,
                                         const char* audience,
                                         gpr_timespec token_lifetime,
                                         const char* scope) {
  if (strcmp(audience, test_service_url_no_service_name) != 0 &&
      strcmp(audience, other_test_service_url_no_service_name) != 0) {
    return nullptr;
  }
  validate_jwt_encode_and_sign_params(json_key, scope, token_lifetime);
  return gpr_strdup(test_signed_jwt);
}

static char* encode_and_sign_jwt_failure(const grpc_auth_json_key* json_key,
                                         const char* /*audience*/,
                                         gpr_timespec token_lifetime,
                                         const char* scope) {
  validate_jwt_encode_and_sign_params(json_key, scope, token_lifetime);
  return nullptr;
}

static char* encode_and_sign_jwt_should_not_be_called(
    const grpc_auth_json_key* /*json_key*/, const char* /*audience*/,
    gpr_timespec /*token_lifetime*/, const char* /*scope*/) {
  GPR_ASSERT("grpc_jwt_encode_and_sign should not be called" == nullptr);
  return nullptr;
}

static grpc_service_account_jwt_access_credentials* creds_as_jwt(
    grpc_call_credentials* creds) {
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(strcmp(creds->type(), GRPC_CALL_CREDENTIALS_TYPE_JWT) == 0);
  return reinterpret_cast<grpc_service_account_jwt_access_credentials*>(creds);
}

static void test_jwt_creds_lifetime(void) {
  char* json_key_string = test_json_key_str();
  const char expected_creds_debug_string_prefix[] =
      "JWTAccessCredentials{ExpirationTime:";
  // Max lifetime.
  grpc_call_credentials* jwt_creds =
      grpc_service_account_jwt_access_credentials_create(
          json_key_string, grpc_max_auth_token_lifetime(), nullptr);
  GPR_ASSERT(gpr_time_cmp(creds_as_jwt(jwt_creds)->jwt_lifetime(),
                          grpc_max_auth_token_lifetime()) == 0);
  /* Check security level. */
  GPR_ASSERT(jwt_creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  GPR_ASSERT(strncmp(expected_creds_debug_string_prefix,
                     jwt_creds->debug_string().c_str(),
                     strlen(expected_creds_debug_string_prefix)) == 0);
  grpc_call_credentials_release(jwt_creds);

  // Shorter lifetime.
  gpr_timespec token_lifetime = {10, 0, GPR_TIMESPAN};
  GPR_ASSERT(gpr_time_cmp(grpc_max_auth_token_lifetime(), token_lifetime) > 0);
  jwt_creds = grpc_service_account_jwt_access_credentials_create(
      json_key_string, token_lifetime, nullptr);
  GPR_ASSERT(gpr_time_cmp(creds_as_jwt(jwt_creds)->jwt_lifetime(),
                          token_lifetime) == 0);
  GPR_ASSERT(strncmp(expected_creds_debug_string_prefix,
                     jwt_creds->debug_string().c_str(),
                     strlen(expected_creds_debug_string_prefix)) == 0);
  grpc_call_credentials_release(jwt_creds);

  // Cropped lifetime.
  gpr_timespec add_to_max = {10, 0, GPR_TIMESPAN};
  token_lifetime = gpr_time_add(grpc_max_auth_token_lifetime(), add_to_max);
  jwt_creds = grpc_service_account_jwt_access_credentials_create(
      json_key_string, token_lifetime, nullptr);
  GPR_ASSERT(gpr_time_cmp(creds_as_jwt(jwt_creds)->jwt_lifetime(),
                          grpc_max_auth_token_lifetime()) == 0);
  GPR_ASSERT(strncmp(expected_creds_debug_string_prefix,
                     jwt_creds->debug_string().c_str(),
                     strlen(expected_creds_debug_string_prefix)) == 0);
  grpc_call_credentials_release(jwt_creds);

  gpr_free(json_key_string);
}

static void test_remove_service_from_jwt_uri(void) {
  const char wrong_uri[] = "hello world";
  GPR_ASSERT(!grpc_core::RemoveServiceNameFromJwtUri(wrong_uri).ok());
  const char valid_uri[] = "https://foo.com/get/";
  const char expected_uri[] = "https://foo.com/";
  auto output = grpc_core::RemoveServiceNameFromJwtUri(valid_uri);
  GPR_ASSERT(output.ok());
  GPR_ASSERT(strcmp(output->c_str(), expected_uri) == 0);
}

static void test_jwt_creds_success(void) {
  const char expected_creds_debug_string_prefix[] =
      "JWTAccessCredentials{ExpirationTime:";

  char* json_key_string = test_json_key_str();
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  std::string expected_md_value = absl::StrCat("Bearer ", test_signed_jwt);
  std::map<std::string, std::string> emd = {
      {"authorization", expected_md_value.c_str()}};
  grpc_call_credentials* creds =
      grpc_service_account_jwt_access_credentials_create(
          json_key_string, grpc_max_auth_token_lifetime(), nullptr);

  /* First request: jwt_encode_and_sign should be called. */
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_jwt_encode_and_sign_set_override(encode_and_sign_jwt_success);
  state->RunRequestMetadataTest(creds, auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();

  /* Second request: the cached token should be served directly. */
  state = RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_jwt_encode_and_sign_set_override(
      encode_and_sign_jwt_should_not_be_called);
  state->RunRequestMetadataTest(creds, auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();

  /* Third request: Different service url so jwt_encode_and_sign should be
     called again (no caching). */
  state = RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  auth_md_ctx.service_url = other_test_service_url;
  grpc_jwt_encode_and_sign_set_override(encode_and_sign_jwt_success);
  state->RunRequestMetadataTest(creds, auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(strncmp(expected_creds_debug_string_prefix,
                     creds->debug_string().c_str(),
                     strlen(expected_creds_debug_string_prefix)) == 0);

  creds->Unref();
  gpr_free(json_key_string);
  grpc_jwt_encode_and_sign_set_override(nullptr);
}

static void test_jwt_creds_signing_failure(void) {
  const char expected_creds_debug_string_prefix[] =
      "JWTAccessCredentials{ExpirationTime:";
  char* json_key_string = test_json_key_str();
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  RequestMetadataState* state = RequestMetadataState::NewInstance(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Could not generate JWT."), {});
  grpc_call_credentials* creds =
      grpc_service_account_jwt_access_credentials_create(
          json_key_string, grpc_max_auth_token_lifetime(), nullptr);

  grpc_jwt_encode_and_sign_set_override(encode_and_sign_jwt_failure);
  state->RunRequestMetadataTest(creds, auth_md_ctx);

  gpr_free(json_key_string);
  GPR_ASSERT(strncmp(expected_creds_debug_string_prefix,
                     creds->debug_string().c_str(),
                     strlen(expected_creds_debug_string_prefix)) == 0);

  creds->Unref();
  grpc_jwt_encode_and_sign_set_override(nullptr);
}

static void set_google_default_creds_env_var_with_file_contents(
    const char* file_prefix, const char* contents) {
  size_t contents_len = strlen(contents);
  char* creds_file_name;
  FILE* creds_file = gpr_tmpfile(file_prefix, &creds_file_name);
  GPR_ASSERT(creds_file_name != nullptr);
  GPR_ASSERT(creds_file != nullptr);
  GPR_ASSERT(fwrite(contents, 1, contents_len, creds_file) == contents_len);
  fclose(creds_file);
  gpr_setenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, creds_file_name);
  gpr_free(creds_file_name);
}

static bool test_gce_tenancy_checker(void) {
  g_test_gce_tenancy_checker_called = true;
  return g_test_is_on_gce;
}

static void test_google_default_creds_auth_key(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_composite_channel_credentials* creds;
  char* json_key = test_json_key_str();
  grpc_flush_cached_google_default_credentials();
  set_gce_tenancy_checker_for_testing(test_gce_tenancy_checker);
  g_test_gce_tenancy_checker_called = false;
  g_test_is_on_gce = true;
  set_google_default_creds_env_var_with_file_contents(
      "json_key_google_default_creds", json_key);
  gpr_free(json_key);
  creds = reinterpret_cast<grpc_composite_channel_credentials*>(
      grpc_google_default_credentials_create(nullptr));
  auto* default_creds =
      reinterpret_cast<const grpc_google_default_channel_credentials*>(
          creds->inner_creds());
  GPR_ASSERT(default_creds->ssl_creds() != nullptr);
  auto* jwt =
      reinterpret_cast<const grpc_service_account_jwt_access_credentials*>(
          creds->call_creds());
  GPR_ASSERT(
      strcmp(jwt->key().client_id,
             "777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent.com") ==
      0);
  GPR_ASSERT(g_test_gce_tenancy_checker_called == false);
  creds->Unref();
  gpr_setenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, ""); /* Reset. */
}

static void test_google_default_creds_refresh_token(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_composite_channel_credentials* creds;
  grpc_flush_cached_google_default_credentials();
  set_google_default_creds_env_var_with_file_contents(
      "refresh_token_google_default_creds", test_refresh_token_str);
  creds = reinterpret_cast<grpc_composite_channel_credentials*>(
      grpc_google_default_credentials_create(nullptr));
  auto* default_creds =
      reinterpret_cast<const grpc_google_default_channel_credentials*>(
          creds->inner_creds());
  GPR_ASSERT(default_creds->ssl_creds() != nullptr);
  auto* refresh =
      reinterpret_cast<const grpc_google_refresh_token_credentials*>(
          creds->call_creds());
  GPR_ASSERT(strcmp(refresh->refresh_token().client_id,
                    "32555999999.apps.googleusercontent.com") == 0);
  creds->Unref();
  gpr_setenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, ""); /* Reset. */
}

static void test_google_default_creds_external_account_credentials(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_composite_channel_credentials* creds;
  grpc_flush_cached_google_default_credentials();
  set_google_default_creds_env_var_with_file_contents(
      "google_default_creds_external_account_credentials",
      test_external_account_credentials_str);
  creds = reinterpret_cast<grpc_composite_channel_credentials*>(
      grpc_google_default_credentials_create(nullptr));
  auto* default_creds =
      reinterpret_cast<const grpc_google_default_channel_credentials*>(
          creds->inner_creds());
  GPR_ASSERT(default_creds->ssl_creds() != nullptr);
  auto* external =
      reinterpret_cast<const grpc_core::ExternalAccountCredentials*>(
          creds->call_creds());
  GPR_ASSERT(external != nullptr);
  creds->Unref();
  gpr_setenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, ""); /* Reset. */
}

static void
test_google_default_creds_external_account_credentials_multi_pattern_sts(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_composite_channel_credentials* creds;
  grpc_flush_cached_google_default_credentials();
  set_google_default_creds_env_var_with_file_contents(
      "google_default_creds_external_account_credentials",
      test_external_account_credentials_multi_pattern_sts_str);
  creds = reinterpret_cast<grpc_composite_channel_credentials*>(
      grpc_google_default_credentials_create(nullptr));
  auto* default_creds =
      reinterpret_cast<const grpc_google_default_channel_credentials*>(
          creds->inner_creds());
  GPR_ASSERT(default_creds->ssl_creds() != nullptr);
  auto* external =
      reinterpret_cast<const grpc_core::ExternalAccountCredentials*>(
          creds->call_creds());
  GPR_ASSERT(external != nullptr);
  creds->Unref();
  gpr_setenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, ""); /* Reset. */
}

static void
test_google_default_creds_external_account_credentials_multi_pattern_iam(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_composite_channel_credentials* creds;
  grpc_flush_cached_google_default_credentials();
  set_google_default_creds_env_var_with_file_contents(
      "google_default_creds_external_account_credentials",
      test_external_account_credentials_multi_pattern_iam_str);
  creds = reinterpret_cast<grpc_composite_channel_credentials*>(
      grpc_google_default_credentials_create(nullptr));
  auto* default_creds =
      reinterpret_cast<const grpc_google_default_channel_credentials*>(
          creds->inner_creds());
  GPR_ASSERT(default_creds->ssl_creds() != nullptr);
  auto* external =
      reinterpret_cast<const grpc_core::ExternalAccountCredentials*>(
          creds->call_creds());
  GPR_ASSERT(external != nullptr);
  creds->Unref();
  gpr_setenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, ""); /* Reset. */
}

static int default_creds_metadata_server_detection_httpcli_get_success_override(
    const grpc_http_request* /*request*/, const char* host, const char* path,
    grpc_millis /*deadline*/, grpc_closure* on_done,
    grpc_http_response* response) {
  *response = http_response(200, "");
  grpc_http_header* headers =
      static_cast<grpc_http_header*>(gpr_malloc(sizeof(*headers) * 1));
  headers[0].key = gpr_strdup("Metadata-Flavor");
  headers[0].value = gpr_strdup("Google");
  response->hdr_count = 1;
  response->hdrs = headers;
  GPR_ASSERT(strcmp(path, "/") == 0);
  GPR_ASSERT(strcmp(host, "metadata.google.internal.") == 0);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

static std::string null_well_known_creds_path_getter(void) { return ""; }

static void test_google_default_creds_gce(void) {
  grpc_core::ExecCtx exec_ctx;
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_"}};
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_flush_cached_google_default_credentials();
  gpr_setenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, ""); /* Reset. */
  grpc_override_well_known_credentials_path_getter(
      null_well_known_creds_path_getter);
  set_gce_tenancy_checker_for_testing(test_gce_tenancy_checker);
  g_test_gce_tenancy_checker_called = false;
  g_test_is_on_gce = true;

  /* Simulate a successful detection of GCE. */
  grpc_composite_channel_credentials* creds =
      reinterpret_cast<grpc_composite_channel_credentials*>(
          grpc_google_default_credentials_create(nullptr));

  /* Verify that the default creds actually embeds a GCE creds. */
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(creds->call_creds() != nullptr);
  grpc_core::HttpRequest::SetOverride(
      compute_engine_httpcli_get_success_override,
      httpcli_post_should_not_be_called);
  state->RunRequestMetadataTest(creds->mutable_call_creds(), auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();

  GPR_ASSERT(g_test_gce_tenancy_checker_called == true);

  /* Cleanup. */
  creds->Unref();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
  grpc_override_well_known_credentials_path_getter(nullptr);
}

static void test_google_default_creds_non_gce(void) {
  grpc_core::ExecCtx exec_ctx;
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_"}};
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_flush_cached_google_default_credentials();
  gpr_setenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, ""); /* Reset. */
  grpc_override_well_known_credentials_path_getter(
      null_well_known_creds_path_getter);
  set_gce_tenancy_checker_for_testing(test_gce_tenancy_checker);
  g_test_gce_tenancy_checker_called = false;
  g_test_is_on_gce = false;
  /* Simulate a successful detection of metadata server. */
  grpc_core::HttpRequest::SetOverride(
      default_creds_metadata_server_detection_httpcli_get_success_override,
      httpcli_post_should_not_be_called);
  grpc_composite_channel_credentials* creds =
      reinterpret_cast<grpc_composite_channel_credentials*>(
          grpc_google_default_credentials_create(nullptr));
  /* Verify that the default creds actually embeds a GCE creds. */
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(creds->call_creds() != nullptr);
  grpc_core::HttpRequest::SetOverride(
      compute_engine_httpcli_get_success_override,
      httpcli_post_should_not_be_called);
  state->RunRequestMetadataTest(creds->mutable_call_creds(), auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(g_test_gce_tenancy_checker_called == true);
  /* Cleanup. */
  creds->Unref();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
  grpc_override_well_known_credentials_path_getter(nullptr);
}

static int default_creds_gce_detection_httpcli_get_failure_override(
    const grpc_http_request* /*request*/, const char* host, const char* path,
    grpc_millis /*deadline*/, grpc_closure* on_done,
    grpc_http_response* response) {
  /* No magic header. */
  GPR_ASSERT(strcmp(path, "/") == 0);
  GPR_ASSERT(strcmp(host, "metadata.google.internal.") == 0);
  *response = http_response(200, "");
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

static void test_no_google_default_creds(void) {
  grpc_flush_cached_google_default_credentials();
  gpr_setenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, ""); /* Reset. */
  grpc_override_well_known_credentials_path_getter(
      null_well_known_creds_path_getter);
  set_gce_tenancy_checker_for_testing(test_gce_tenancy_checker);
  g_test_gce_tenancy_checker_called = false;
  g_test_is_on_gce = false;
  grpc_core::HttpRequest::SetOverride(
      default_creds_gce_detection_httpcli_get_failure_override,
      httpcli_post_should_not_be_called);
  /* Simulate a successful detection of GCE. */
  GPR_ASSERT(grpc_google_default_credentials_create(nullptr) == nullptr);
  /* Try a second one. GCE detection should occur again. */
  g_test_gce_tenancy_checker_called = false;
  GPR_ASSERT(grpc_google_default_credentials_create(nullptr) == nullptr);
  GPR_ASSERT(g_test_gce_tenancy_checker_called == true);
  /* Cleanup. */
  grpc_override_well_known_credentials_path_getter(nullptr);
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
}

static void test_google_default_creds_call_creds_specified(void) {
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer ya29.AHES6ZRN3-HlhAPya30GnW_bHSb_"}};
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_core::ExecCtx exec_ctx;
  grpc_flush_cached_google_default_credentials();
  grpc_call_credentials* call_creds =
      grpc_google_compute_engine_credentials_create(nullptr);
  set_gce_tenancy_checker_for_testing(test_gce_tenancy_checker);
  g_test_gce_tenancy_checker_called = false;
  g_test_is_on_gce = true;
  grpc_core::HttpRequest::SetOverride(
      default_creds_metadata_server_detection_httpcli_get_success_override,
      httpcli_post_should_not_be_called);
  grpc_composite_channel_credentials* channel_creds =
      reinterpret_cast<grpc_composite_channel_credentials*>(
          grpc_google_default_credentials_create(call_creds));
  GPR_ASSERT(g_test_gce_tenancy_checker_called == false);
  GPR_ASSERT(channel_creds != nullptr);
  GPR_ASSERT(channel_creds->call_creds() != nullptr);
  grpc_core::HttpRequest::SetOverride(
      compute_engine_httpcli_get_success_override,
      httpcli_post_should_not_be_called);
  state->RunRequestMetadataTest(channel_creds->mutable_call_creds(),
                                auth_md_ctx);

  grpc_core::ExecCtx::Get()->Flush();
  channel_creds->Unref();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
}

struct fake_call_creds : public grpc_call_credentials {
 public:
  fake_call_creds() : grpc_call_credentials("fake") {}

  bool get_request_metadata(grpc_polling_entity* /*pollent*/,
                            grpc_auth_metadata_context /*context*/,
                            grpc_core::CredentialsMetadataArray* md_array,
                            grpc_closure* /*on_request_metadata*/,
                            grpc_error_handle* /*error*/) override {
    md_array->emplace_back(grpc_core::Slice::FromStaticString("foo"),
                           grpc_core::Slice::FromStaticString("oof"));
    return true;
  }

  void cancel_get_request_metadata(
      grpc_core::CredentialsMetadataArray* /*md_array*/,
      grpc_error_handle /*error*/) override {}
};

static void test_google_default_creds_not_default(void) {
  std::map<std::string, std::string> emd = {{"foo", "oof"}};
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_core::ExecCtx exec_ctx;
  grpc_flush_cached_google_default_credentials();
  grpc_core::RefCountedPtr<grpc_call_credentials> call_creds =
      grpc_core::MakeRefCounted<fake_call_creds>();
  set_gce_tenancy_checker_for_testing(test_gce_tenancy_checker);
  g_test_gce_tenancy_checker_called = false;
  g_test_is_on_gce = true;
  grpc_core::HttpRequest::SetOverride(
      default_creds_metadata_server_detection_httpcli_get_success_override,
      httpcli_post_should_not_be_called);
  grpc_composite_channel_credentials* channel_creds =
      reinterpret_cast<grpc_composite_channel_credentials*>(
          grpc_google_default_credentials_create(call_creds.release()));
  GPR_ASSERT(g_test_gce_tenancy_checker_called == false);
  GPR_ASSERT(channel_creds != nullptr);
  GPR_ASSERT(channel_creds->call_creds() != nullptr);
  state->RunRequestMetadataTest(channel_creds->mutable_call_creds(),
                                auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  channel_creds->Unref();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
}

typedef enum {
  PLUGIN_INITIAL_STATE,
  PLUGIN_GET_METADATA_CALLED_STATE,
  PLUGIN_DESTROY_CALLED_STATE
} plugin_state;

static const std::map<std::string, std::string> plugin_md = {{"foo", "bar"},
                                                             {"hi", "there"}};

static int plugin_get_metadata_success(
    void* state, grpc_auth_metadata_context context,
    grpc_credentials_plugin_metadata_cb /*cb*/, void* /*user_data*/,
    grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
    size_t* num_creds_md, grpc_status_code* /*status*/,
    const char** /*error_details*/) {
  GPR_ASSERT(strcmp(context.service_url, test_service_url) == 0);
  GPR_ASSERT(strcmp(context.method_name, test_method) == 0);
  GPR_ASSERT(context.channel_auth_context == nullptr);
  GPR_ASSERT(context.reserved == nullptr);
  GPR_ASSERT(plugin_md.size() < GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX);
  plugin_state* s = static_cast<plugin_state*>(state);
  *s = PLUGIN_GET_METADATA_CALLED_STATE;
  size_t i = 0;
  for (auto const& md : plugin_md) {
    memset(&creds_md[i], 0, sizeof(grpc_metadata));
    creds_md[i].key = grpc_slice_from_copied_string(md.first.c_str());
    creds_md[i].value = grpc_slice_from_copied_string(md.second.c_str());
    i += 1;
  }
  *num_creds_md = plugin_md.size();
  return true;  // Synchronous return.
}

static const char* plugin_error_details = "Could not get metadata for plugin.";

static int plugin_get_metadata_failure(
    void* state, grpc_auth_metadata_context context,
    grpc_credentials_plugin_metadata_cb /*cb*/, void* /*user_data*/,
    grpc_metadata /*creds_md*/[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
    size_t* /*num_creds_md*/, grpc_status_code* status,
    const char** error_details) {
  GPR_ASSERT(strcmp(context.service_url, test_service_url) == 0);
  GPR_ASSERT(strcmp(context.method_name, test_method) == 0);
  GPR_ASSERT(context.channel_auth_context == nullptr);
  GPR_ASSERT(context.reserved == nullptr);
  plugin_state* s = static_cast<plugin_state*>(state);
  *s = PLUGIN_GET_METADATA_CALLED_STATE;
  *status = GRPC_STATUS_UNAUTHENTICATED;
  *error_details = gpr_strdup(plugin_error_details);
  return true;  // Synchronous return.
}

static void plugin_destroy(void* state) {
  plugin_state* s = static_cast<plugin_state*>(state);
  *s = PLUGIN_DESTROY_CALLED_STATE;
}

static char* plugin_debug_string(void* state) {
  plugin_state* s = static_cast<plugin_state*>(state);
  char* ret = nullptr;
  switch (*s) {
    case PLUGIN_INITIAL_STATE:
      gpr_asprintf(&ret, "TestPluginCredentials{state:INITIAL}");
      break;
    case PLUGIN_GET_METADATA_CALLED_STATE:
      gpr_asprintf(&ret, "TestPluginCredentials{state:GET_METADATA_CALLED}");
      break;
    case PLUGIN_DESTROY_CALLED_STATE:
      gpr_asprintf(&ret, "TestPluginCredentials{state:DESTROY}");
      break;
    default:
      gpr_asprintf(&ret, "TestPluginCredentials{state:UNKNOWN}");
      break;
  }
  return ret;
}

static void test_metadata_plugin_success(void) {
  const char expected_creds_debug_string[] =
      "TestPluginCredentials{state:GET_METADATA_CALLED}";
  plugin_state state = PLUGIN_INITIAL_STATE;
  grpc_metadata_credentials_plugin plugin;
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  RequestMetadataState* md_state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, plugin_md);

  plugin.state = &state;
  plugin.get_metadata = plugin_get_metadata_success;
  plugin.destroy = plugin_destroy;
  plugin.debug_string = plugin_debug_string;

  grpc_call_credentials* creds = grpc_metadata_credentials_create_from_plugin(
      plugin, GRPC_PRIVACY_AND_INTEGRITY, nullptr);
  /* Check security level. */
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  GPR_ASSERT(state == PLUGIN_INITIAL_STATE);
  md_state->RunRequestMetadataTest(creds, auth_md_ctx);
  GPR_ASSERT(state == PLUGIN_GET_METADATA_CALLED_STATE);
  GPR_ASSERT(
      strcmp(creds->debug_string().c_str(), expected_creds_debug_string) == 0);
  creds->Unref();

  GPR_ASSERT(state == PLUGIN_DESTROY_CALLED_STATE);
}

static void test_metadata_plugin_failure(void) {
  const char expected_creds_debug_string[] =
      "TestPluginCredentials{state:GET_METADATA_CALLED}";

  plugin_state state = PLUGIN_INITIAL_STATE;
  grpc_metadata_credentials_plugin plugin;
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  RequestMetadataState* md_state = RequestMetadataState::NewInstance(
      GRPC_ERROR_CREATE_FROM_CPP_STRING(
          absl::StrCat("Getting metadata from plugin failed with error: ",
                       plugin_error_details)),
      {});

  plugin.state = &state;
  plugin.get_metadata = plugin_get_metadata_failure;
  plugin.destroy = plugin_destroy;
  plugin.debug_string = plugin_debug_string;

  grpc_call_credentials* creds = grpc_metadata_credentials_create_from_plugin(
      plugin, GRPC_PRIVACY_AND_INTEGRITY, nullptr);
  GPR_ASSERT(state == PLUGIN_INITIAL_STATE);
  md_state->RunRequestMetadataTest(creds, auth_md_ctx);
  GPR_ASSERT(state == PLUGIN_GET_METADATA_CALLED_STATE);
  GPR_ASSERT(
      strcmp(creds->debug_string().c_str(), expected_creds_debug_string) == 0);
  creds->Unref();

  GPR_ASSERT(state == PLUGIN_DESTROY_CALLED_STATE);
}

static void test_get_well_known_google_credentials_file_path(void) {
  char* home = gpr_getenv("HOME");
  bool restore_home_env = false;
#if defined(GRPC_BAZEL_BUILD) && \
    (defined(GPR_POSIX_ENV) || defined(GPR_LINUX_ENV))
  // when running under bazel locally, the HOME variable is not set
  // so we set it to some fake value
  restore_home_env = true;
  gpr_setenv("HOME", "/fake/home/for/bazel");
#endif /* defined(GRPC_BAZEL_BUILD) && (defined(GPR_POSIX_ENV) || \
          defined(GPR_LINUX_ENV)) */
  std::string path = grpc_get_well_known_google_credentials_file_path();
  GPR_ASSERT(!path.empty());
#if defined(GPR_POSIX_ENV) || defined(GPR_LINUX_ENV)
  restore_home_env = true;
  gpr_unsetenv("HOME");
  path = grpc_get_well_known_google_credentials_file_path();
  GPR_ASSERT(path.empty());
#endif /* GPR_POSIX_ENV || GPR_LINUX_ENV */
  if (restore_home_env) {
    if (home) {
      gpr_setenv("HOME", home);
    } else {
      gpr_unsetenv("HOME");
    }
  }
  gpr_free(home);
}

static void test_channel_creds_duplicate_without_call_creds(void) {
  const char expected_creds_debug_string[] =
      "AccessTokenCredentials{Token:present}";
  grpc_core::ExecCtx exec_ctx;

  grpc_channel_credentials* channel_creds =
      grpc_fake_transport_security_credentials_create();

  grpc_core::RefCountedPtr<grpc_channel_credentials> dup =
      channel_creds->duplicate_without_call_credentials();
  GPR_ASSERT(dup == channel_creds);
  dup.reset();

  grpc_call_credentials* call_creds =
      grpc_access_token_credentials_create("blah", nullptr);
  grpc_channel_credentials* composite_creds =
      grpc_composite_channel_credentials_create(channel_creds, call_creds,
                                                nullptr);
  GPR_ASSERT(strcmp(call_creds->debug_string().c_str(),
                    expected_creds_debug_string) == 0);

  call_creds->Unref();
  dup = composite_creds->duplicate_without_call_credentials();
  GPR_ASSERT(dup == channel_creds);
  dup.reset();

  channel_creds->Unref();
  composite_creds->Unref();
}

typedef struct {
  const char* url_scheme;
  const char* call_host;
  const char* call_method;
  const char* desired_service_url;
  const char* desired_method_name;
} auth_metadata_context_test_case;

static void test_auth_metadata_context(void) {
  auth_metadata_context_test_case test_cases[] = {
      // No service nor method.
      {"https", "www.foo.com", "", "https://www.foo.com", ""},
      // No method.
      {"https", "www.foo.com", "/Service", "https://www.foo.com/Service", ""},
      // Empty service and method.
      {"https", "www.foo.com", "//", "https://www.foo.com/", ""},
      // Empty method.
      {"https", "www.foo.com", "/Service/", "https://www.foo.com/Service", ""},
      // Malformed url.
      {"https", "www.foo.com:", "/Service/", "https://www.foo.com:/Service",
       ""},
      // https, default explicit port.
      {"https", "www.foo.com:443", "/Service/FooMethod",
       "https://www.foo.com/Service", "FooMethod"},
      // https, default implicit port.
      {"https", "www.foo.com", "/Service/FooMethod",
       "https://www.foo.com/Service", "FooMethod"},
      // https with ipv6 literal, default explicit port.
      {"https", "[1080:0:0:0:8:800:200C:417A]:443", "/Service/FooMethod",
       "https://[1080:0:0:0:8:800:200C:417A]/Service", "FooMethod"},
      // https with ipv6 literal, default implicit port.
      {"https", "[1080:0:0:0:8:800:200C:443]", "/Service/FooMethod",
       "https://[1080:0:0:0:8:800:200C:443]/Service", "FooMethod"},
      // https, custom port.
      {"https", "www.foo.com:8888", "/Service/FooMethod",
       "https://www.foo.com:8888/Service", "FooMethod"},
      // https with ipv6 literal, custom port.
      {"https", "[1080:0:0:0:8:800:200C:417A]:8888", "/Service/FooMethod",
       "https://[1080:0:0:0:8:800:200C:417A]:8888/Service", "FooMethod"},
      // custom url scheme, https default port.
      {"blah", "www.foo.com:443", "/Service/FooMethod",
       "blah://www.foo.com:443/Service", "FooMethod"}};
  for (uint32_t i = 0; i < GPR_ARRAY_SIZE(test_cases); i++) {
    const char* url_scheme = test_cases[i].url_scheme;
    grpc_slice call_host =
        grpc_slice_from_copied_string(test_cases[i].call_host);
    grpc_slice call_method =
        grpc_slice_from_copied_string(test_cases[i].call_method);
    grpc_auth_metadata_context auth_md_context;
    memset(&auth_md_context, 0, sizeof(auth_md_context));
    grpc_auth_metadata_context_build(url_scheme, call_host, call_method,
                                     nullptr, &auth_md_context);
    if (strcmp(auth_md_context.service_url,
               test_cases[i].desired_service_url) != 0) {
      gpr_log(GPR_ERROR, "Invalid service url, want: %s, got %s.",
              test_cases[i].desired_service_url, auth_md_context.service_url);
      GPR_ASSERT(false);
    }
    if (strcmp(auth_md_context.method_name,
               test_cases[i].desired_method_name) != 0) {
      gpr_log(GPR_ERROR, "Invalid method name, want: %s, got %s.",
              test_cases[i].desired_method_name, auth_md_context.method_name);
      GPR_ASSERT(false);
    }
    GPR_ASSERT(auth_md_context.channel_auth_context == nullptr);
    grpc_slice_unref(call_host);
    grpc_slice_unref(call_method);
    grpc_auth_metadata_context_reset(&auth_md_context);
  }
}

static void validate_external_account_creds_token_exchage_request(
    const grpc_http_request* request, const char* host, const char* path,
    const char* body, size_t body_size, bool /*expect_actor_token*/) {
  // Check that the body is constructed properly.
  GPR_ASSERT(body != nullptr);
  GPR_ASSERT(body_size != 0);
  std::string get_url_equivalent =
      absl::StrFormat("%s?%s", "https://foo.com:5555/token", body);
  absl::StatusOr<grpc_core::URI> uri =
      grpc_core::URI::Parse(get_url_equivalent);
  if (!uri.ok()) {
    gpr_log(GPR_ERROR, "%s", uri.status().ToString().c_str());
    GPR_ASSERT(uri.ok());
  }
  assert_query_parameters(*uri, "audience", "audience");
  assert_query_parameters(*uri, "grant_type",
                          "urn:ietf:params:oauth:grant-type:token-exchange");
  assert_query_parameters(*uri, "requested_token_type",
                          "urn:ietf:params:oauth:token-type:access_token");
  assert_query_parameters(*uri, "subject_token", "test_subject_token");
  assert_query_parameters(*uri, "subject_token_type", "subject_token_type");
  assert_query_parameters(*uri, "scope",
                          "https://www.googleapis.com/auth/cloud-platform");

  // Check the rest of the request.
  GPR_ASSERT(strcmp(host, "foo.com:5555") == 0);
  GPR_ASSERT(strcmp(path, "/token") == 0);
  GPR_ASSERT(request->hdr_count == 2);
  GPR_ASSERT(strcmp(request->hdrs[0].key, "Content-Type") == 0);
  GPR_ASSERT(
      strcmp(request->hdrs[0].value, "application/x-www-form-urlencoded") == 0);
  GPR_ASSERT(strcmp(request->hdrs[1].key, "Authorization") == 0);
  GPR_ASSERT(strcmp(request->hdrs[1].value,
                    "Basic Y2xpZW50X2lkOmNsaWVudF9zZWNyZXQ=") == 0);
}

static void
validate_external_account_creds_token_exchage_request_with_url_encode(
    const grpc_http_request* request, const char* host, const char* path,
    const char* body, size_t body_size, bool /*expect_actor_token*/) {
  // Check that the body is constructed properly.
  GPR_ASSERT(body != nullptr);
  GPR_ASSERT(body_size != 0);
  GPR_ASSERT(
      strcmp(
          std::string(body, body_size).c_str(),
          "audience=audience_!%40%23%24&grant_type=urn%3Aietf%3Aparams%3Aoauth%"
          "3Agrant-type%3Atoken-exchange&requested_token_type=urn%3Aietf%"
          "3Aparams%3Aoauth%3Atoken-type%3Aaccess_token&subject_token_type="
          "subject_token_type_!%40%23%24&subject_token=test_subject_token&"
          "scope=https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fcloud-platform&"
          "options=%7B%7D") == 0);

  // Check the rest of the request.
  GPR_ASSERT(strcmp(host, "foo.com:5555") == 0);
  GPR_ASSERT(strcmp(path, "/token_url_encode") == 0);
  GPR_ASSERT(request->hdr_count == 2);
  GPR_ASSERT(strcmp(request->hdrs[0].key, "Content-Type") == 0);
  GPR_ASSERT(
      strcmp(request->hdrs[0].value, "application/x-www-form-urlencoded") == 0);
  GPR_ASSERT(strcmp(request->hdrs[1].key, "Authorization") == 0);
  GPR_ASSERT(strcmp(request->hdrs[1].value,
                    "Basic Y2xpZW50X2lkOmNsaWVudF9zZWNyZXQ=") == 0);
}

static void
validate_external_account_creds_service_account_impersonation_request(
    const grpc_http_request* request, const char* host, const char* path,
    const char* body, size_t body_size, bool /*expect_actor_token*/) {
  // Check that the body is constructed properly.
  GPR_ASSERT(body != nullptr);
  GPR_ASSERT(body_size != 0);
  GPR_ASSERT(strcmp(body, "scope=scope_1 scope_2") == 0);
  // Check the rest of the request.
  GPR_ASSERT(strcmp(host, "foo.com:5555") == 0);
  GPR_ASSERT(strcmp(path, "/service_account_impersonation") == 0);
  GPR_ASSERT(request->hdr_count == 2);
  GPR_ASSERT(strcmp(request->hdrs[0].key, "Content-Type") == 0);
  GPR_ASSERT(
      strcmp(request->hdrs[0].value, "application/x-www-form-urlencoded") == 0);
  GPR_ASSERT(strcmp(request->hdrs[1].key, "Authorization") == 0);
  GPR_ASSERT(strcmp(request->hdrs[1].value,
                    "Bearer token_exchange_access_token") == 0);
}

static int external_account_creds_httpcli_post_success(
    const grpc_http_request* request, const char* host, const char* path,
    const char* body, size_t body_size, grpc_millis /*deadline*/,
    grpc_closure* on_done, grpc_http_response* response) {
  if (strcmp(path, "/token") == 0) {
    validate_external_account_creds_token_exchage_request(
        request, host, path, body, body_size, true);
    *response = http_response(
        200, valid_external_account_creds_token_exchange_response);
  } else if (strcmp(path, "/service_account_impersonation") == 0) {
    validate_external_account_creds_service_account_impersonation_request(
        request, host, path, body, body_size, true);
    *response = http_response(
        200,
        valid_external_account_creds_service_account_impersonation_response);
  } else if (strcmp(path, "/token_url_encode") == 0) {
    validate_external_account_creds_token_exchage_request_with_url_encode(
        request, host, path, body, body_size, true);
    *response = http_response(
        200, valid_external_account_creds_token_exchange_response);
  }
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

static int
external_account_creds_httpcli_post_failure_token_exchange_response_missing_access_token(
    const grpc_http_request* /*request*/, const char* /*host*/,
    const char* path, const char* /*body*/, size_t /*body_size*/,
    grpc_millis /*deadline*/, grpc_closure* on_done,
    grpc_http_response* response) {
  if (strcmp(path, "/token") == 0) {
    *response = http_response(200,
                              "{\"not_access_token\":\"not_access_token\","
                              "\"expires_in\":3599,"
                              " \"token_type\":\"Bearer\"}");
  } else if (strcmp(path, "/service_account_impersonation") == 0) {
    *response = http_response(
        200,
        valid_external_account_creds_service_account_impersonation_response);
  }
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

static int url_external_account_creds_httpcli_get_success(
    const grpc_http_request* /*request*/, const char* /*host*/,
    const char* path, grpc_millis /*deadline*/, grpc_closure* on_done,
    grpc_http_response* response) {
  if (strcmp(path, "/generate_subject_token_format_text") == 0) {
    *response = http_response(
        200,
        valid_url_external_account_creds_retrieve_subject_token_response_format_text);
  } else if (strcmp(path, "/path/to/url/creds?p1=v1&p2=v2") == 0) {
    *response = http_response(
        200,
        valid_url_external_account_creds_retrieve_subject_token_response_format_text);
  } else if (strcmp(path, "/generate_subject_token_format_json") == 0) {
    *response = http_response(
        200,
        valid_url_external_account_creds_retrieve_subject_token_response_format_json);
  }
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

static void validate_aws_external_account_creds_token_exchage_request(
    const grpc_http_request* request, const char* host, const char* path,
    const char* body, size_t body_size, bool /*expect_actor_token*/) {
  // Check that the body is constructed properly.
  GPR_ASSERT(body != nullptr);
  GPR_ASSERT(body_size != 0);
  // Check that the regional_cred_verification_url got constructed
  // with the correct AWS Region ("test_regionz" or "test_region").
  GPR_ASSERT(strstr(body, "regional_cred_verification_url_test_region"));
  std::string get_url_equivalent =
      absl::StrFormat("%s?%s", "https://foo.com:5555/token", body);
  absl::StatusOr<grpc_core::URI> uri =
      grpc_core::URI::Parse(get_url_equivalent);
  GPR_ASSERT(uri.ok());
  assert_query_parameters(*uri, "audience", "audience");
  assert_query_parameters(*uri, "grant_type",
                          "urn:ietf:params:oauth:grant-type:token-exchange");
  assert_query_parameters(*uri, "requested_token_type",
                          "urn:ietf:params:oauth:token-type:access_token");
  assert_query_parameters(*uri, "subject_token_type", "subject_token_type");
  assert_query_parameters(*uri, "scope",
                          "https://www.googleapis.com/auth/cloud-platform");
  // Check the rest of the request.
  GPR_ASSERT(strcmp(host, "foo.com:5555") == 0);
  GPR_ASSERT(strcmp(path, "/token") == 0);
  GPR_ASSERT(request->hdr_count == 2);
  GPR_ASSERT(strcmp(request->hdrs[0].key, "Content-Type") == 0);
  GPR_ASSERT(
      strcmp(request->hdrs[0].value, "application/x-www-form-urlencoded") == 0);
  GPR_ASSERT(strcmp(request->hdrs[1].key, "Authorization") == 0);
  GPR_ASSERT(strcmp(request->hdrs[1].value,
                    "Basic Y2xpZW50X2lkOmNsaWVudF9zZWNyZXQ=") == 0);
}

static int aws_external_account_creds_httpcli_get_success(
    const grpc_http_request* /*request*/, const char* /*host*/,
    const char* path, grpc_millis /*deadline*/, grpc_closure* on_done,
    grpc_http_response* response) {
  if (strcmp(path, "/region_url") == 0) {
    *response = http_response(200, "test_regionz");
  } else if (strcmp(path, "/url") == 0) {
    *response = http_response(200, "test_role_name");
  } else if (strcmp(path, "/url_no_role_name") == 0) {
    *response = http_response(200, "");
  } else if (strcmp(path, "/url/test_role_name") == 0) {
    *response = http_response(
        200, valid_aws_external_account_creds_retrieve_signing_keys_response);
  }
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

static int aws_external_account_creds_httpcli_post_success(
    const grpc_http_request* request, const char* host, const char* path,
    const char* body, size_t body_size, grpc_millis /*deadline*/,
    grpc_closure* on_done, grpc_http_response* response) {
  if (strcmp(path, "/token") == 0) {
    validate_aws_external_account_creds_token_exchage_request(
        request, host, path, body, body_size, true);
    *response = http_response(
        200, valid_external_account_creds_token_exchange_response);
  }
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, GRPC_ERROR_NONE);
  return 1;
}

// The subclass of ExternalAccountCredentials for testing.
// ExternalAccountCredentials is an abstract class so we can't directly test
// against it.
class TestExternalAccountCredentials final
    : public grpc_core::ExternalAccountCredentials {
 public:
  TestExternalAccountCredentials(Options options,
                                 std::vector<std::string> scopes)
      : ExternalAccountCredentials(std::move(options), std::move(scopes)) {}

 protected:
  void RetrieveSubjectToken(
      HTTPRequestContext* /*ctx*/, const Options& /*options*/,
      std::function<void(std::string, grpc_error_handle)> cb) override {
    cb("test_subject_token", GRPC_ERROR_NONE);
  }
};

static void test_external_account_creds_success(void) {
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer token_exchange_access_token"}};
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_core::Json credential_source("");
  TestExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  TestExternalAccountCredentials creds(options, {});
  /* Check security level. */
  GPR_ASSERT(creds.min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  /* First request: http put should be called. */
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_core::HttpRequest::SetOverride(
      httpcli_get_should_not_be_called,
      external_account_creds_httpcli_post_success);
  state->RunRequestMetadataTest(&creds, auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  /* Second request: the cached token should be served directly. */
  state = RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_core::HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                                      httpcli_post_should_not_be_called);
  state->RunRequestMetadataTest(&creds, auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
}

static void test_external_account_creds_success_with_url_encode(void) {
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer token_exchange_access_token"}};
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_core::Json credential_source("");
  TestExternalAccountCredentials::Options options = {
      "external_account",         // type;
      "audience_!@#$",            // audience;
      "subject_token_type_!@#$",  // subject_token_type;
      "",                         // service_account_impersonation_url;
      "https://foo.com:5555/token_url_encode",  // token_url;
      "https://foo.com:5555/token_info",        // token_info_url;
      credential_source,                        // credential_source;
      "quota_project_id",                       // quota_project_id;
      "client_id",                              // client_id;
      "client_secret",                          // client_secret;
      "",                                       // workforce_pool_user_project;
  };
  TestExternalAccountCredentials creds(options, {});
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_core::HttpRequest::SetOverride(
      httpcli_get_should_not_be_called,
      external_account_creds_httpcli_post_success);
  state->RunRequestMetadataTest(&creds, auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
}

static void
test_external_account_creds_success_with_service_account_impersonation(void) {
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer service_account_impersonation_access_token"}};
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_core::Json credential_source("");
  TestExternalAccountCredentials::Options options = {
      "external_account",    // type;
      "audience",            // audience;
      "subject_token_type",  // subject_token_type;
      "https://foo.com:5555/service_account_impersonation",  // service_account_impersonation_url;
      "https://foo.com:5555/token",                          // token_url;
      "https://foo.com:5555/token_info",                     // token_info_url;
      credential_source,   // credential_source;
      "quota_project_id",  // quota_project_id;
      "client_id",         // client_id;
      "client_secret",     // client_secret;
      "",                  // workforce_pool_user_project;
  };
  TestExternalAccountCredentials creds(options, {"scope_1", "scope_2"});
  /* Check security level. */
  GPR_ASSERT(creds.min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  /* First request: http put should be called. */
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_core::HttpRequest::SetOverride(
      httpcli_get_should_not_be_called,
      external_account_creds_httpcli_post_success);
  state->RunRequestMetadataTest(&creds, auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
}

static void test_external_account_creds_failure_invalid_token_url(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_core::Json credential_source("");
  TestExternalAccountCredentials::Options options = {
      "external_account",    // type;
      "audience",            // audience;
      "subject_token_type",  // subject_token_type;
      "https://foo.com:5555/service_account_impersonation",  // service_account_impersonation_url;
      "invalid_token_url",                                   // token_url;
      "https://foo.com:5555/token_info",                     // token_info_url;
      credential_source,   // credential_source;
      "quota_project_id",  // quota_project_id;
      "client_id",         // client_id;
      "client_secret",     // client_secret;
      "",                  // workforce_pool_user_project;
  };
  TestExternalAccountCredentials creds(options, {});
  grpc_core::HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                                      httpcli_post_should_not_be_called);
  grpc_error_handle error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
      "Invalid token url: invalid_token_url.");
  grpc_error_handle expected_error =
      GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
          "Error occurred when fetching oauth2 token.", &error, 1);
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(expected_error, {});
  state->RunRequestMetadataTest(&creds, auth_md_ctx);
  GRPC_ERROR_UNREF(error);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
}

static void
test_external_account_creds_failure_invalid_service_account_impersonation_url(
    void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_core::Json credential_source("");
  TestExternalAccountCredentials::Options options = {
      "external_account",                           // type;
      "audience",                                   // audience;
      "subject_token_type",                         // subject_token_type;
      "invalid_service_account_impersonation_url",  // service_account_impersonation_url;
      "https://foo.com:5555/token",                 // token_url;
      "https://foo.com:5555/token_info",            // token_info_url;
      credential_source,                            // credential_source;
      "quota_project_id",                           // quota_project_id;
      "client_id",                                  // client_id;
      "client_secret",                              // client_secret;
      "",  // workforce_pool_user_project;
  };
  TestExternalAccountCredentials creds(options, {});
  grpc_core::HttpRequest::SetOverride(
      httpcli_get_should_not_be_called,
      external_account_creds_httpcli_post_success);
  grpc_error_handle error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
      "Invalid service account impersonation url: "
      "invalid_service_account_impersonation_url.");
  grpc_error_handle expected_error =
      GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
          "Error occurred when fetching oauth2 token.", &error, 1);
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(expected_error, {});
  state->RunRequestMetadataTest(&creds, auth_md_ctx);
  GRPC_ERROR_UNREF(error);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
}

static void
test_external_account_creds_failure_token_exchange_response_missing_access_token(
    void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_core::Json credential_source("");
  TestExternalAccountCredentials::Options options = {
      "external_account",    // type;
      "audience",            // audience;
      "subject_token_type",  // subject_token_type;
      "https://foo.com:5555/service_account_impersonation",  // service_account_impersonation_url;
      "https://foo.com:5555/token",                          // token_url;
      "https://foo.com:5555/token_info",                     // token_info_url;
      credential_source,   // credential_source;
      "quota_project_id",  // quota_project_id;
      "client_id",         // client_id;
      "client_secret",     // client_secret;
      "",                  // workforce_pool_user_project;
  };
  TestExternalAccountCredentials creds(options, {});
  grpc_core::HttpRequest::SetOverride(
      httpcli_get_should_not_be_called,
      external_account_creds_httpcli_post_failure_token_exchange_response_missing_access_token);
  grpc_error_handle error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
      "Missing or invalid access_token in "
      "{\"not_access_token\":\"not_access_token\",\"expires_in\":3599,\"token_"
      "type\":\"Bearer\"}.");
  grpc_error_handle expected_error =
      GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
          "Error occurred when fetching oauth2 token.", &error, 1);
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(expected_error, {});
  state->RunRequestMetadataTest(&creds, auth_md_ctx);
  GRPC_ERROR_UNREF(error);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
}

static void test_url_external_account_creds_success_format_text(void) {
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer token_exchange_access_token"}};
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_core::Json credential_source = grpc_core::Json::Parse(
      valid_url_external_account_creds_options_credential_source_format_text,
      &error);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_core::ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      grpc_core::UrlExternalAccountCredentials::Create(options, {}, &error);
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_core::HttpRequest::SetOverride(
      url_external_account_creds_httpcli_get_success,
      external_account_creds_httpcli_post_success);
  state->RunRequestMetadataTest(creds.get(), auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
}

static void
test_url_external_account_creds_success_with_qurey_params_format_text(void) {
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer token_exchange_access_token"}};
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_core::Json credential_source = grpc_core::Json::Parse(
      valid_url_external_account_creds_options_credential_source_with_qurey_params_format_text,
      &error);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_core::ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      grpc_core::UrlExternalAccountCredentials::Create(options, {}, &error);
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_core::HttpRequest::SetOverride(
      url_external_account_creds_httpcli_get_success,
      external_account_creds_httpcli_post_success);
  state->RunRequestMetadataTest(creds.get(), auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
}

static void test_url_external_account_creds_success_format_json(void) {
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer token_exchange_access_token"}};
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_core::Json credential_source = grpc_core::Json::Parse(
      valid_url_external_account_creds_options_credential_source_format_json,
      &error);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_core::ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      grpc_core::UrlExternalAccountCredentials::Create(options, {}, &error);
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_core::HttpRequest::SetOverride(
      url_external_account_creds_httpcli_get_success,
      external_account_creds_httpcli_post_success);
  state->RunRequestMetadataTest(creds.get(), auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
}

static void
test_url_external_account_creds_failure_invalid_credential_source_url(void) {
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_core::Json credential_source = grpc_core::Json::Parse(
      invalid_url_external_account_creds_options_credential_source, &error);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_core::ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      grpc_core::UrlExternalAccountCredentials::Create(options, {}, &error);
  GPR_ASSERT(creds == nullptr);
  std::string actual_error;
  GPR_ASSERT(
      grpc_error_get_str(error, GRPC_ERROR_STR_DESCRIPTION, &actual_error));
  GPR_ASSERT(absl::StartsWith(actual_error, "Invalid credential source url."));
  GRPC_ERROR_UNREF(error);
}

static void test_file_external_account_creds_success_format_text(void) {
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer token_exchange_access_token"}};
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_error_handle error = GRPC_ERROR_NONE;
  char* subject_token_path = write_tmp_jwt_file("test_subject_token");
  grpc_core::Json credential_source = grpc_core::Json::Parse(
      absl::StrFormat(
          "{\"file\":\"%s\"}",
          absl::StrReplaceAll(subject_token_path, {{"\\", "\\\\"}})),
      &error);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_core::ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      grpc_core::FileExternalAccountCredentials::Create(options, {}, &error);
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_core::HttpRequest::SetOverride(
      httpcli_get_should_not_be_called,
      external_account_creds_httpcli_post_success);
  state->RunRequestMetadataTest(creds.get(), auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
  GRPC_ERROR_UNREF(error);
  gpr_free(subject_token_path);
}

static void test_file_external_account_creds_success_format_json(void) {
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer token_exchange_access_token"}};
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_error_handle error = GRPC_ERROR_NONE;
  char* subject_token_path =
      write_tmp_jwt_file("{\"access_token\":\"test_subject_token\"}");
  grpc_core::Json credential_source = grpc_core::Json::Parse(
      absl::StrFormat(
          "{\n"
          "\"file\":\"%s\",\n"
          "\"format\":\n"
          "{\n"
          "\"type\":\"json\",\n"
          "\"subject_token_field_name\":\"access_token\"\n"
          "}\n"
          "}",
          absl::StrReplaceAll(subject_token_path, {{"\\", "\\\\"}})),
      &error);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_core::ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      grpc_core::FileExternalAccountCredentials::Create(options, {}, &error);
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_core::HttpRequest::SetOverride(
      httpcli_get_should_not_be_called,
      external_account_creds_httpcli_post_success);
  state->RunRequestMetadataTest(creds.get(), auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
  GRPC_ERROR_UNREF(error);
  gpr_free(subject_token_path);
}

static void test_file_external_account_creds_failure_file_not_found(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_core::Json credential_source =
      grpc_core::Json::Parse("{\"file\":\"non_exisiting_file\"}", &error);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_core::ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      grpc_core::FileExternalAccountCredentials::Create(options, {}, &error);
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_core::HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                                      httpcli_post_should_not_be_called);
  error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Failed to load file");
  grpc_error_handle expected_error =
      GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
          "Error occurred when fetching oauth2 token.", &error, 1);
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(expected_error, {});
  state->RunRequestMetadataTest(creds.get(), auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
  GRPC_ERROR_UNREF(error);
}

static void test_file_external_account_creds_failure_invalid_json_content(
    void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_error_handle error = GRPC_ERROR_NONE;
  char* subject_token_path = write_tmp_jwt_file("not_a_valid_json_file");
  grpc_core::Json credential_source = grpc_core::Json::Parse(
      absl::StrFormat(
          "{\n"
          "\"file\":\"%s\",\n"
          "\"format\":\n"
          "{\n"
          "\"type\":\"json\",\n"
          "\"subject_token_field_name\":\"access_token\"\n"
          "}\n"
          "}",
          absl::StrReplaceAll(subject_token_path, {{"\\", "\\\\"}})),
      &error);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_core::ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      grpc_core::FileExternalAccountCredentials::Create(options, {}, &error);
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_core::HttpRequest::SetOverride(httpcli_get_should_not_be_called,
                                      httpcli_post_should_not_be_called);
  error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
      "The content of the file is not a valid json object.");
  grpc_error_handle expected_error =
      GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
          "Error occurred when fetching oauth2 token.", &error, 1);
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(expected_error, {});
  state->RunRequestMetadataTest(creds.get(), auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
  GRPC_ERROR_UNREF(error);
  gpr_free(subject_token_path);
}

static void test_aws_external_account_creds_success(void) {
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer token_exchange_access_token"}};
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_core::Json credential_source = grpc_core::Json::Parse(
      valid_aws_external_account_creds_options_credential_source, &error);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_core::ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      grpc_core::AwsExternalAccountCredentials::Create(options, {}, &error);
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_core::HttpRequest::SetOverride(
      aws_external_account_creds_httpcli_get_success,
      aws_external_account_creds_httpcli_post_success);
  state->RunRequestMetadataTest(creds.get(), auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
}

static void test_aws_external_account_creds_success_path_region_env_keys_url(
    void) {
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer token_exchange_access_token"}};
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  gpr_setenv("AWS_REGION", "test_regionz");
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_core::Json credential_source = grpc_core::Json::Parse(
      valid_aws_external_account_creds_options_credential_source, &error);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_core::ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      grpc_core::AwsExternalAccountCredentials::Create(options, {}, &error);
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_core::HttpRequest::SetOverride(
      aws_external_account_creds_httpcli_get_success,
      aws_external_account_creds_httpcli_post_success);
  state->RunRequestMetadataTest(creds.get(), auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
  gpr_unsetenv("AWS_REGION");
}

static void
test_aws_external_account_creds_success_path_default_region_env_keys_url(void) {
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer token_exchange_access_token"}};
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  gpr_setenv("AWS_DEFAULT_REGION", "test_regionz");
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_core::Json credential_source = grpc_core::Json::Parse(
      valid_aws_external_account_creds_options_credential_source, &error);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_core::ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      grpc_core::AwsExternalAccountCredentials::Create(options, {}, &error);
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_core::HttpRequest::SetOverride(
      aws_external_account_creds_httpcli_get_success,
      aws_external_account_creds_httpcli_post_success);
  state->RunRequestMetadataTest(creds.get(), auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
  gpr_unsetenv("AWS_DEFAULT_REGION");
}

static void
test_aws_external_account_creds_success_path_duplicate_region_env_keys_url(
    void) {
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer token_exchange_access_token"}};
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  // Make sure that AWS_REGION gets used over AWS_DEFAULT_REGION
  gpr_setenv("AWS_REGION", "test_regionz");
  gpr_setenv("AWS_DEFAULT_REGION", "ERROR_REGION");
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_core::Json credential_source = grpc_core::Json::Parse(
      valid_aws_external_account_creds_options_credential_source, &error);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_core::ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      grpc_core::AwsExternalAccountCredentials::Create(options, {}, &error);
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_core::HttpRequest::SetOverride(
      aws_external_account_creds_httpcli_get_success,
      aws_external_account_creds_httpcli_post_success);
  state->RunRequestMetadataTest(creds.get(), auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
  gpr_unsetenv("AWS_REGION");
  gpr_unsetenv("AWS_DEFAULT_REGION");
}

static void test_aws_external_account_creds_success_path_region_url_keys_env(
    void) {
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer token_exchange_access_token"}};
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  gpr_setenv("AWS_ACCESS_KEY_ID", "test_access_key_id");
  gpr_setenv("AWS_SECRET_ACCESS_KEY", "test_secret_access_key");
  gpr_setenv("AWS_SESSION_TOKEN", "test_token");
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_core::Json credential_source = grpc_core::Json::Parse(
      valid_aws_external_account_creds_options_credential_source, &error);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_core::ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      grpc_core::AwsExternalAccountCredentials::Create(options, {}, &error);
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_core::HttpRequest::SetOverride(
      aws_external_account_creds_httpcli_get_success,
      aws_external_account_creds_httpcli_post_success);
  state->RunRequestMetadataTest(creds.get(), auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
  gpr_unsetenv("AWS_ACCESS_KEY_ID");
  gpr_unsetenv("AWS_SECRET_ACCESS_KEY");
  gpr_unsetenv("AWS_SESSION_TOKEN");
}

static void test_aws_external_account_creds_success_path_region_env_keys_env(
    void) {
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer token_exchange_access_token"}};
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  gpr_setenv("AWS_REGION", "test_regionz");
  gpr_setenv("AWS_ACCESS_KEY_ID", "test_access_key_id");
  gpr_setenv("AWS_SECRET_ACCESS_KEY", "test_secret_access_key");
  gpr_setenv("AWS_SESSION_TOKEN", "test_token");
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_core::Json credential_source = grpc_core::Json::Parse(
      valid_aws_external_account_creds_options_credential_source, &error);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_core::ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      grpc_core::AwsExternalAccountCredentials::Create(options, {}, &error);
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_core::HttpRequest::SetOverride(
      aws_external_account_creds_httpcli_get_success,
      aws_external_account_creds_httpcli_post_success);
  state->RunRequestMetadataTest(creds.get(), auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
  gpr_unsetenv("AWS_REGION");
  gpr_unsetenv("AWS_ACCESS_KEY_ID");
  gpr_unsetenv("AWS_SECRET_ACCESS_KEY");
  gpr_unsetenv("AWS_SESSION_TOKEN");
}

static void
test_aws_external_account_creds_success_path_default_region_env_keys_env(void) {
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer token_exchange_access_token"}};
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  gpr_setenv("AWS_DEFAULT_REGION", "test_regionz");
  gpr_setenv("AWS_ACCESS_KEY_ID", "test_access_key_id");
  gpr_setenv("AWS_SECRET_ACCESS_KEY", "test_secret_access_key");
  gpr_setenv("AWS_SESSION_TOKEN", "test_token");
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_core::Json credential_source = grpc_core::Json::Parse(
      valid_aws_external_account_creds_options_credential_source, &error);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_core::ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      grpc_core::AwsExternalAccountCredentials::Create(options, {}, &error);
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_core::HttpRequest::SetOverride(
      aws_external_account_creds_httpcli_get_success,
      aws_external_account_creds_httpcli_post_success);
  state->RunRequestMetadataTest(creds.get(), auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
  gpr_unsetenv("AWS_DEFAULT_REGION");
  gpr_unsetenv("AWS_ACCESS_KEY_ID");
  gpr_unsetenv("AWS_SECRET_ACCESS_KEY");
  gpr_unsetenv("AWS_SESSION_TOKEN");
}

static void
test_aws_external_account_creds_success_path_duplicate_region_env_keys_env(
    void) {
  std::map<std::string, std::string> emd = {
      {"authorization", "Bearer token_exchange_access_token"}};
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  // Make sure that AWS_REGION gets used over AWS_DEFAULT_REGION
  gpr_setenv("AWS_REGION", "test_regionz");
  gpr_setenv("AWS_DEFAULT_REGION", "ERROR_REGION");
  gpr_setenv("AWS_ACCESS_KEY_ID", "test_access_key_id");
  gpr_setenv("AWS_SECRET_ACCESS_KEY", "test_secret_access_key");
  gpr_setenv("AWS_SESSION_TOKEN", "test_token");
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_core::Json credential_source = grpc_core::Json::Parse(
      valid_aws_external_account_creds_options_credential_source, &error);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_core::ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      grpc_core::AwsExternalAccountCredentials::Create(options, {}, &error);
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(GRPC_ERROR_NONE, emd);
  grpc_core::HttpRequest::SetOverride(
      aws_external_account_creds_httpcli_get_success,
      aws_external_account_creds_httpcli_post_success);
  state->RunRequestMetadataTest(creds.get(), auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
  gpr_unsetenv("AWS_REGION");
  gpr_unsetenv("AWS_DEFAULT_REGION");
  gpr_unsetenv("AWS_ACCESS_KEY_ID");
  gpr_unsetenv("AWS_SECRET_ACCESS_KEY");
  gpr_unsetenv("AWS_SESSION_TOKEN");
}

static void test_aws_external_account_creds_failure_unmatched_environment_id(
    void) {
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_core::Json credential_source = grpc_core::Json::Parse(
      invalid_aws_external_account_creds_options_credential_source_unmatched_environment_id,
      &error);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_core::ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      grpc_core::AwsExternalAccountCredentials::Create(options, {}, &error);
  GPR_ASSERT(creds == nullptr);
  std::string expected_error = "environment_id does not match.";
  std::string actual_error;
  GPR_ASSERT(
      grpc_error_get_str(error, GRPC_ERROR_STR_DESCRIPTION, &actual_error));
  GPR_ASSERT(expected_error == actual_error);
  GRPC_ERROR_UNREF(error);
}

static void test_aws_external_account_creds_failure_invalid_region_url(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_core::Json credential_source = grpc_core::Json::Parse(
      invalid_aws_external_account_creds_options_credential_source_invalid_region_url,
      &error);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_core::ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      grpc_core::AwsExternalAccountCredentials::Create(options, {}, &error);
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
      "Invalid region url: invalid_region_url.");
  grpc_error_handle expected_error =
      GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
          "Error occurred when fetching oauth2 token.", &error, 1);
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(expected_error, {});
  grpc_core::HttpRequest::SetOverride(
      aws_external_account_creds_httpcli_get_success,
      aws_external_account_creds_httpcli_post_success);
  state->RunRequestMetadataTest(creds.get(), auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
  GRPC_ERROR_UNREF(error);
}

static void test_aws_external_account_creds_failure_invalid_url(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_core::Json credential_source = grpc_core::Json::Parse(
      invalid_aws_external_account_creds_options_credential_source_invalid_url,
      &error);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_core::ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      grpc_core::AwsExternalAccountCredentials::Create(options, {}, &error);
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Invalid url: invalid_url.");
  grpc_error_handle expected_error =
      GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
          "Error occurred when fetching oauth2 token.", &error, 1);
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(expected_error, {});
  grpc_core::HttpRequest::SetOverride(
      aws_external_account_creds_httpcli_get_success,
      aws_external_account_creds_httpcli_post_success);
  state->RunRequestMetadataTest(creds.get(), auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
  GRPC_ERROR_UNREF(error);
}

static void test_aws_external_account_creds_failure_missing_role_name(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_core::Json credential_source = grpc_core::Json::Parse(
      invalid_aws_external_account_creds_options_credential_source_missing_role_name,
      &error);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_core::ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      grpc_core::AwsExternalAccountCredentials::Create(options, {}, &error);
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
      "Missing role name when retrieving signing keys.");
  grpc_error_handle expected_error =
      GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
          "Error occurred when fetching oauth2 token.", &error, 1);
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(expected_error, {});
  grpc_core::HttpRequest::SetOverride(
      aws_external_account_creds_httpcli_get_success,
      aws_external_account_creds_httpcli_post_success);
  state->RunRequestMetadataTest(creds.get(), auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
  GRPC_ERROR_UNREF(error);
}

static void
test_aws_external_account_creds_failure_invalid_regional_cred_verification_url(
    void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_auth_metadata_context auth_md_ctx = {test_service_url, test_method,
                                            nullptr, nullptr};
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_core::Json credential_source = grpc_core::Json::Parse(
      invalid_aws_external_account_creds_options_credential_source_invalid_regional_cred_verification_url,
      &error);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_core::ExternalAccountCredentials::Options options = {
      "external_account",                 // type;
      "audience",                         // audience;
      "subject_token_type",               // subject_token_type;
      "",                                 // service_account_impersonation_url;
      "https://foo.com:5555/token",       // token_url;
      "https://foo.com:5555/token_info",  // token_info_url;
      credential_source,                  // credential_source;
      "quota_project_id",                 // quota_project_id;
      "client_id",                        // client_id;
      "client_secret",                    // client_secret;
      "",                                 // workforce_pool_user_project;
  };
  auto creds =
      grpc_core::AwsExternalAccountCredentials::Create(options, {}, &error);
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(creds->min_security_level() == GRPC_PRIVACY_AND_INTEGRITY);
  error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
      "Creating aws request signer failed.");
  grpc_error_handle expected_error =
      GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
          "Error occurred when fetching oauth2 token.", &error, 1);
  RequestMetadataState* state =
      RequestMetadataState::NewInstance(expected_error, {});
  grpc_core::HttpRequest::SetOverride(
      aws_external_account_creds_httpcli_get_success,
      aws_external_account_creds_httpcli_post_success);
  state->RunRequestMetadataTest(creds.get(), auth_md_ctx);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::HttpRequest::SetOverride(nullptr, nullptr);
  GRPC_ERROR_UNREF(error);
}

static void test_external_account_credentials_create_success(void) {
  // url credentials
  const char* url_options_string =
      "{\"type\":\"external_account\",\"audience\":\"audience\",\"subject_"
      "token_type\":\"subject_token_type\",\"service_account_impersonation_"
      "url\":\"service_account_impersonation_url\",\"token_url\":\"https://"
      "foo.com:5555/token\",\"token_info_url\":\"https://foo.com:5555/"
      "token_info\",\"credential_source\":{\"url\":\"https://foo.com:5555/"
      "generate_subject_token_format_json\",\"headers\":{\"Metadata-Flavor\":"
      "\"Google\"},\"format\":{\"type\":\"json\",\"subject_token_field_name\":"
      "\"access_token\"}},\"quota_project_id\":\"quota_"
      "project_id\",\"client_id\":\"client_id\",\"client_secret\":\"client_"
      "secret\"}";
  const char* url_scopes_string = "scope1,scope2";
  grpc_call_credentials* url_creds = grpc_external_account_credentials_create(
      url_options_string, url_scopes_string);
  GPR_ASSERT(url_creds != nullptr);
  url_creds->Unref();
  // file credentials
  const char* file_options_string =
      "{\"type\":\"external_account\",\"audience\":\"audience\",\"subject_"
      "token_type\":\"subject_token_type\",\"service_account_impersonation_"
      "url\":\"service_account_impersonation_url\",\"token_url\":\"https://"
      "foo.com:5555/token\",\"token_info_url\":\"https://foo.com:5555/"
      "token_info\",\"credential_source\":{\"file\":\"credentials_file_path\"},"
      "\"quota_project_id\":\"quota_"
      "project_id\",\"client_id\":\"client_id\",\"client_secret\":\"client_"
      "secret\"}";
  const char* file_scopes_string = "scope1,scope2";
  grpc_call_credentials* file_creds = grpc_external_account_credentials_create(
      file_options_string, file_scopes_string);
  GPR_ASSERT(file_creds != nullptr);
  file_creds->Unref();
  // aws credentials
  const char* aws_options_string =
      "{\"type\":\"external_account\",\"audience\":\"audience\",\"subject_"
      "token_type\":\"subject_token_type\",\"service_account_impersonation_"
      "url\":\"service_account_impersonation_url\",\"token_url\":\"https://"
      "foo.com:5555/token\",\"token_info_url\":\"https://foo.com:5555/"
      "token_info\",\"credential_source\":{\"environment_id\":\"aws1\","
      "\"region_url\":\"https://foo.com:5555/region_url\",\"url\":\"https://"
      "foo.com:5555/url\",\"regional_cred_verification_url\":\"https://"
      "foo.com:5555/regional_cred_verification_url_{region}\"},"
      "\"quota_project_id\":\"quota_"
      "project_id\",\"client_id\":\"client_id\",\"client_secret\":\"client_"
      "secret\"}";
  const char* aws_scopes_string = "scope1,scope2";
  grpc_call_credentials* aws_creds = grpc_external_account_credentials_create(
      aws_options_string, aws_scopes_string);
  GPR_ASSERT(aws_creds != nullptr);
  aws_creds->Unref();
}

static void
test_external_account_credentials_create_failure_invalid_json_format(void) {
  const char* options_string = "invalid_json";
  grpc_call_credentials* creds =
      grpc_external_account_credentials_create(options_string, "");
  GPR_ASSERT(creds == nullptr);
}

static void
test_external_account_credentials_create_failure_invalid_options_format(void) {
  const char* options_string = "{\"random_key\":\"random_value\"}";
  grpc_call_credentials* creds =
      grpc_external_account_credentials_create(options_string, "");
  GPR_ASSERT(creds == nullptr);
}

static void
test_external_account_credentials_create_failure_invalid_options_credential_source(
    void) {
  const char* options_string =
      "{\"type\":\"external_account\",\"audience\":\"audience\",\"subject_"
      "token_type\":\"subject_token_type\",\"service_account_impersonation_"
      "url\":\"service_account_impersonation_url\",\"token_url\":\"https://"
      "foo.com:5555/token\",\"token_info_url\":\"https://foo.com:5555/"
      "token_info\",\"credential_source\":{\"random_key\":\"random_value\"},"
      "\"quota_project_id\":\"quota_"
      "project_id\",\"client_id\":\"client_id\",\"client_secret\":\"client_"
      "secret\"}";
  grpc_call_credentials* creds =
      grpc_external_account_credentials_create(options_string, "");
  GPR_ASSERT(creds == nullptr);
}

static void test_external_account_credentials_create_success_workforce_pool(
    void) {
  const char* url_options_string =
      "{\"type\":\"external_account\",\"audience\":\"//iam.googleapis.com/"
      "locations/location/workforcePools/pool/providers/provider\",\"subject_"
      "token_type\":\"subject_token_type\",\"service_account_impersonation_"
      "url\":\"service_account_impersonation_url\",\"token_url\":\"https://"
      "foo.com:5555/token\",\"token_info_url\":\"https://foo.com:5555/"
      "token_info\",\"credential_source\":{\"url\":\"https://foo.com:5555/"
      "generate_subject_token_format_json\",\"headers\":{\"Metadata-Flavor\":"
      "\"Google\"},\"format\":{\"type\":\"json\",\"subject_token_field_name\":"
      "\"access_token\"}},\"quota_project_id\":\"quota_"
      "project_id\",\"client_id\":\"client_id\",\"client_secret\":\"client_"
      "secret\",\"workforce_pool_user_project\":\"workforce_pool_user_"
      "project\"}";
  const char* url_scopes_string = "scope1,scope2";
  grpc_call_credentials* url_creds = grpc_external_account_credentials_create(
      url_options_string, url_scopes_string);
  GPR_ASSERT(url_creds != nullptr);
  url_creds->Unref();
}

static void
test_external_account_credentials_create_failure_invalid_workforce_pool_audience(
    void) {
  const char* url_options_string =
      "{\"type\":\"external_account\",\"audience\":\"invalid_workforce_pool_"
      "audience\",\"subject_"
      "token_type\":\"subject_token_type\",\"service_account_impersonation_"
      "url\":\"service_account_impersonation_url\",\"token_url\":\"https://"
      "foo.com:5555/token\",\"token_info_url\":\"https://foo.com:5555/"
      "token_info\",\"credential_source\":{\"url\":\"https://foo.com:5555/"
      "generate_subject_token_format_json\",\"headers\":{\"Metadata-Flavor\":"
      "\"Google\"},\"format\":{\"type\":\"json\",\"subject_token_field_name\":"
      "\"access_token\"}},\"quota_project_id\":\"quota_"
      "project_id\",\"client_id\":\"client_id\",\"client_secret\":\"client_"
      "secret\",\"workforce_pool_user_project\":\"workforce_pool_user_"
      "project\"}";
  const char* url_scopes_string = "scope1,scope2";
  grpc_call_credentials* url_creds = grpc_external_account_credentials_create(
      url_options_string, url_scopes_string);
  GPR_ASSERT(url_creds == nullptr);
}

static void test_insecure_credentials_compare_success(void) {
  auto* insecure_creds_1 = grpc_insecure_credentials_create();
  auto* insecure_creds_2 = grpc_insecure_credentials_create();
  GPR_ASSERT(grpc_core::QsortCompare(insecure_creds_1, insecure_creds_2) == 0);
  grpc_arg arg_1 = grpc_channel_credentials_to_arg(insecure_creds_1);
  grpc_channel_args args_1 = {1, &arg_1};
  grpc_arg arg_2 = grpc_channel_credentials_to_arg(insecure_creds_2);
  grpc_channel_args args_2 = {1, &arg_2};
  GPR_ASSERT(grpc_channel_args_compare(&args_1, &args_2) == 0);
  grpc_channel_credentials_release(insecure_creds_1);
  grpc_channel_credentials_release(insecure_creds_2);
}

static void test_insecure_credentials_compare_failure(void) {
  auto* insecure_creds = grpc_insecure_credentials_create();
  auto* fake_creds = grpc_fake_transport_security_credentials_create();
  GPR_ASSERT(grpc_core::QsortCompare(insecure_creds, fake_creds) != 0);
  grpc_arg arg_1 = grpc_channel_credentials_to_arg(insecure_creds);
  grpc_channel_args args_1 = {1, &arg_1};
  grpc_arg arg_2 = grpc_channel_credentials_to_arg(fake_creds);
  grpc_channel_args args_2 = {1, &arg_2};
  GPR_ASSERT(grpc_channel_args_compare(&args_1, &args_2) != 0);
  grpc_channel_credentials_release(insecure_creds);
  grpc_channel_credentials_release(fake_creds);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  test_oauth2_token_fetcher_creds_parsing_ok();
  test_oauth2_token_fetcher_creds_parsing_bad_http_status();
  test_oauth2_token_fetcher_creds_parsing_empty_http_body();
  test_oauth2_token_fetcher_creds_parsing_invalid_json();
  test_oauth2_token_fetcher_creds_parsing_missing_token();
  test_oauth2_token_fetcher_creds_parsing_missing_token_type();
  test_oauth2_token_fetcher_creds_parsing_missing_token_lifetime();
  test_google_iam_creds();
  test_access_token_creds();
  test_channel_oauth2_composite_creds();
  test_oauth2_google_iam_composite_creds();
  test_channel_oauth2_google_iam_composite_creds();
  test_compute_engine_creds_success();
  test_compute_engine_creds_failure();
  test_refresh_token_creds_success();
  test_refresh_token_creds_failure();
  test_valid_sts_creds_options();
  test_invalid_sts_creds_options();
  test_sts_creds_success();
  test_sts_creds_no_actor_token_success();
  test_sts_creds_load_token_failure();
  test_sts_creds_http_failure();
  test_sts_creds_token_file_not_found();
  test_jwt_creds_lifetime();
  test_jwt_creds_success();
  test_jwt_creds_signing_failure();
  test_remove_service_from_jwt_uri();
  test_google_default_creds_auth_key();
  test_google_default_creds_refresh_token();
  test_google_default_creds_external_account_credentials();
  test_google_default_creds_external_account_credentials_multi_pattern_sts();
  test_google_default_creds_external_account_credentials_multi_pattern_iam();
  test_google_default_creds_gce();
  test_google_default_creds_non_gce();
  test_no_google_default_creds();
  test_google_default_creds_call_creds_specified();
  test_google_default_creds_not_default();
  test_metadata_plugin_success();
  test_metadata_plugin_failure();
  test_get_well_known_google_credentials_file_path();
  test_channel_creds_duplicate_without_call_creds();
  test_auth_metadata_context();
  test_external_account_creds_success();
  test_external_account_creds_success_with_url_encode();
  test_external_account_creds_success_with_service_account_impersonation();
  test_external_account_creds_failure_invalid_token_url();
  test_external_account_creds_failure_invalid_service_account_impersonation_url();
  test_external_account_creds_failure_token_exchange_response_missing_access_token();
  test_url_external_account_creds_success_format_text();
  test_url_external_account_creds_success_format_json();
  test_url_external_account_creds_failure_invalid_credential_source_url();
  test_url_external_account_creds_success_with_qurey_params_format_text();
  test_file_external_account_creds_success_format_text();
  test_file_external_account_creds_success_format_json();
  test_file_external_account_creds_failure_file_not_found();
  test_file_external_account_creds_failure_invalid_json_content();
  test_aws_external_account_creds_success();
  test_aws_external_account_creds_success_path_region_env_keys_url();
  test_aws_external_account_creds_success_path_default_region_env_keys_url();
  test_aws_external_account_creds_success_path_duplicate_region_env_keys_url();
  test_aws_external_account_creds_success_path_region_url_keys_env();
  test_aws_external_account_creds_success_path_region_env_keys_env();
  test_aws_external_account_creds_success_path_default_region_env_keys_env();
  test_aws_external_account_creds_success_path_duplicate_region_env_keys_env();
  test_aws_external_account_creds_failure_unmatched_environment_id();
  test_aws_external_account_creds_failure_invalid_region_url();
  test_aws_external_account_creds_failure_invalid_url();
  test_aws_external_account_creds_failure_missing_role_name();
  test_aws_external_account_creds_failure_invalid_regional_cred_verification_url();
  test_external_account_credentials_create_success();
  test_external_account_credentials_create_failure_invalid_json_format();
  test_external_account_credentials_create_failure_invalid_options_format();
  test_external_account_credentials_create_failure_invalid_options_credential_source();
  test_external_account_credentials_create_success_workforce_pool();
  test_external_account_credentials_create_failure_invalid_workforce_pool_audience();
  test_insecure_credentials_compare_success();
  test_insecure_credentials_compare_failure();
  grpc_shutdown();
  return 0;
}
