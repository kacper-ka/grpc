// Copyright 2021 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/authorization/sdk_server_authz_filter.h"

#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/security/authorization/evaluate_args.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

TraceFlag grpc_sdk_authz_trace(false, "sdk_authz");

SdkServerAuthzFilter::SdkServerAuthzFilter(
    RefCountedPtr<grpc_auth_context> auth_context, grpc_endpoint* endpoint,
    RefCountedPtr<grpc_authorization_policy_provider> provider)
    : auth_context_(std::move(auth_context)),
      per_channel_evaluate_args_(auth_context_.get(), endpoint),
      provider_(std::move(provider)) {}

absl::StatusOr<SdkServerAuthzFilter> SdkServerAuthzFilter::Create(
    const grpc_channel_args* args) {
  grpc_auth_context* auth_context = grpc_find_auth_context_in_args(args);
  grpc_authorization_policy_provider* provider =
      grpc_channel_args_find_pointer<grpc_authorization_policy_provider>(
          args, GRPC_ARG_AUTHORIZATION_POLICY_PROVIDER);
  if (provider == nullptr) {
    return absl::InvalidArgumentError("Failed to get authorization provider.");
  }
  // grpc_endpoint isn't needed because the current SDK authorization policy
  // does not support any rules that requires looking for source or destination
  // addresses.
  return SdkServerAuthzFilter(
      auth_context != nullptr ? auth_context->Ref() : nullptr,
      /*endpoint=*/nullptr, provider->Ref());
}

bool SdkServerAuthzFilter::IsAuthorized(
    const ClientInitialMetadata& initial_metadata) {
  EvaluateArgs args(initial_metadata.get(), &per_channel_evaluate_args_);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_sdk_authz_trace)) {
    gpr_log(GPR_DEBUG,
            "checking request: url_path=%s, transport_security_type=%s, "
            "uri_sans=[%s], dns_sans=[%s], subject=%s",
            std::string(args.GetPath()).c_str(),
            std::string(args.GetTransportSecurityType()).c_str(),
            absl::StrJoin(args.GetUriSans(), ",").c_str(),
            absl::StrJoin(args.GetDnsSans(), ",").c_str(),
            std::string(args.GetSubject()).c_str());
  }
  grpc_authorization_policy_provider::AuthorizationEngines engines =
      provider_->engines();
  if (engines.deny_engine != nullptr) {
    AuthorizationEngine::Decision decision =
        engines.deny_engine->Evaluate(args);
    if (decision.type == AuthorizationEngine::Decision::Type::kDeny) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_sdk_authz_trace)) {
        gpr_log(GPR_INFO, "chand=%p: request denied by policy %s.", this,
                decision.matching_policy_name.c_str());
      }
      return false;
    }
  }
  if (engines.allow_engine != nullptr) {
    AuthorizationEngine::Decision decision =
        engines.allow_engine->Evaluate(args);
    if (decision.type == AuthorizationEngine::Decision::Type::kAllow) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_sdk_authz_trace)) {
        gpr_log(GPR_DEBUG, "chand=%p: request allowed by policy %s.", this,
                decision.matching_policy_name.c_str());
      }
      return true;
    }
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_sdk_authz_trace)) {
    gpr_log(GPR_INFO, "chand=%p: request denied, no matching policy found.",
            this);
  }
  return false;
}

ArenaPromise<TrailingMetadata> SdkServerAuthzFilter::MakeCallPromise(
    ClientInitialMetadata initial_metadata,
    NextPromiseFactory next_promise_factory) {
  if (!IsAuthorized(initial_metadata)) {
    return ArenaPromise<TrailingMetadata>(Immediate(TrailingMetadata(
        absl::PermissionDeniedError("Unauthorized RPC request rejected."))));
  }
  return next_promise_factory(std::move(initial_metadata));
}

const grpc_channel_filter SdkServerAuthzFilter::kFilterVtable =
    MakePromiseBasedFilter<SdkServerAuthzFilter, FilterEndpoint::kServer>(
        "sdk-server-authz");

}  // namespace grpc_core
