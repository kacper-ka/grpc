/*
 *
 * Copyright 2016 gRPC authors.
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

#include <grpc/grpc.h>

#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/surface/builtins.h"

extern void grpc_register_extra_plugins(void);

void grpc_chttp2_plugin_init(void);
void grpc_chttp2_plugin_shutdown(void);
void grpc_client_channel_init(void);
void grpc_client_channel_shutdown(void);
void grpc_resolver_fake_init(void);
void grpc_resolver_fake_shutdown(void);
void grpc_lb_policy_grpclb_init(void);
void grpc_lb_policy_grpclb_shutdown(void);
void grpc_lb_policy_priority_init(void);
void grpc_lb_policy_priority_shutdown(void);
void grpc_lb_policy_weighted_target_init(void);
void grpc_lb_policy_weighted_target_shutdown(void);
void grpc_lb_policy_pick_first_init(void);
void grpc_lb_policy_pick_first_shutdown(void);
void grpc_lb_policy_round_robin_init(void);
void grpc_lb_policy_round_robin_shutdown(void);
void grpc_resolver_dns_ares_init(void);
void grpc_resolver_dns_ares_shutdown(void);
void grpc_resolver_dns_native_init(void);
void grpc_resolver_dns_native_shutdown(void);
void grpc_resolver_sockaddr_init(void);
void grpc_resolver_sockaddr_shutdown(void);
void grpc_message_size_filter_init(void);
void grpc_message_size_filter_shutdown(void);
namespace grpc_core {
void FaultInjectionFilterInit(void);
void FaultInjectionFilterShutdown(void);
void GrpcLbPolicyRingHashInit(void);
void GrpcLbPolicyRingHashShutdown(void);
#ifndef GRPC_NO_RLS
void RlsLbPluginInit();
void RlsLbPluginShutdown();
#endif  // !GRPC_NO_RLS
void ServiceConfigParserInit(void);
void ServiceConfigParserShutdown(void);
}  // namespace grpc_core

#ifdef GPR_SUPPORT_BINDER_TRANSPORT
void grpc_resolver_binder_init(void);
void grpc_resolver_binder_shutdown(void);
#endif

void grpc_register_built_in_plugins(void) {
  grpc_register_plugin(grpc_chttp2_plugin_init, grpc_chttp2_plugin_shutdown);
  grpc_register_plugin(grpc_core::ServiceConfigParserInit,
                       grpc_core::ServiceConfigParserShutdown);
  grpc_register_plugin(grpc_client_channel_init, grpc_client_channel_shutdown);
  grpc_register_plugin(grpc_resolver_fake_init, grpc_resolver_fake_shutdown);
  grpc_register_plugin(grpc_lb_policy_grpclb_init,
                       grpc_lb_policy_grpclb_shutdown);
#ifndef GRPC_NO_RLS
  grpc_register_plugin(grpc_core::RlsLbPluginInit,
                       grpc_core::RlsLbPluginShutdown);
#endif  // !GRPC_NO_RLS
  grpc_register_plugin(grpc_lb_policy_priority_init,
                       grpc_lb_policy_priority_shutdown);
  grpc_register_plugin(grpc_lb_policy_weighted_target_init,
                       grpc_lb_policy_weighted_target_shutdown);
  grpc_register_plugin(grpc_lb_policy_pick_first_init,
                       grpc_lb_policy_pick_first_shutdown);
  grpc_register_plugin(grpc_lb_policy_round_robin_init,
                       grpc_lb_policy_round_robin_shutdown);
  grpc_register_plugin(grpc_core::GrpcLbPolicyRingHashInit,
                       grpc_core::GrpcLbPolicyRingHashShutdown);
  grpc_register_plugin(grpc_resolver_dns_ares_init,
                       grpc_resolver_dns_ares_shutdown);
  grpc_register_plugin(grpc_resolver_dns_native_init,
                       grpc_resolver_dns_native_shutdown);
  grpc_register_plugin(grpc_resolver_sockaddr_init,
                       grpc_resolver_sockaddr_shutdown);
  grpc_register_plugin(grpc_message_size_filter_init,
                       grpc_message_size_filter_shutdown);
  grpc_register_plugin(grpc_core::FaultInjectionFilterInit,
                       grpc_core::FaultInjectionFilterShutdown);
#ifdef GPR_SUPPORT_BINDER_TRANSPORT
  grpc_register_plugin(grpc_resolver_binder_init,
                       grpc_resolver_binder_shutdown);
#endif
  grpc_register_extra_plugins();
}

namespace grpc_core {

extern void BuildClientChannelConfiguration(
    CoreConfiguration::Builder* builder);
extern void SecurityRegisterHandshakerFactories(
    CoreConfiguration::Builder* builder);
extern void RegisterClientAuthorityFilter(CoreConfiguration::Builder* builder);
extern void RegisterClientIdleFilter(CoreConfiguration::Builder* builder);
extern void RegisterDeadlineFilter(CoreConfiguration::Builder* builder);
extern void RegisterGrpcLbLoadReportingFilter(
    CoreConfiguration::Builder* builder);
extern void RegisterHttpFilters(CoreConfiguration::Builder* builder);
extern void RegisterMaxAgeFilter(CoreConfiguration::Builder* builder);
extern void RegisterMessageSizeFilter(CoreConfiguration::Builder* builder);
extern void RegisterSecurityFilters(CoreConfiguration::Builder* builder);
extern void RegisterServiceConfigChannelArgFilter(
    CoreConfiguration::Builder* builder);
extern void RegisterExtraFilters(CoreConfiguration::Builder* builder);
extern void RegisterResourceQuota(CoreConfiguration::Builder* builder);

void BuildCoreConfiguration(CoreConfiguration::Builder* builder) {
  BuildClientChannelConfiguration(builder);
  SecurityRegisterHandshakerFactories(builder);
  RegisterClientAuthorityFilter(builder);
  RegisterClientIdleFilter(builder);
  RegisterGrpcLbLoadReportingFilter(builder);
  RegisterHttpFilters(builder);
  RegisterMaxAgeFilter(builder);
  RegisterDeadlineFilter(builder);
  RegisterMessageSizeFilter(builder);
  RegisterServiceConfigChannelArgFilter(builder);
  RegisterResourceQuota(builder);
  // Run last so it gets a consistent location.
  // TODO(ctiller): Is this actually necessary?
  RegisterSecurityFilters(builder);
  RegisterExtraFilters(builder);
  RegisterBuiltins(builder);
}

}  // namespace grpc_core
