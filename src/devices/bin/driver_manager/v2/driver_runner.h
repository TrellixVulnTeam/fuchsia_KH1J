// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_V2_DRIVER_RUNNER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_V2_DRIVER_RUNNER_H_

#include <fidl/fuchsia.component/cpp/wire.h>
#include <fidl/fuchsia.driver.development/cpp/wire.h>
#include <fidl/fuchsia.driver.host/cpp/wire.h>
#include <fidl/fuchsia.driver.index/cpp/wire.h>
#include <fidl/fuchsia.ldsvc/cpp/wire.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/wire/client.h>
#include <lib/fidl/cpp/wire/wire_messaging.h>
#include <lib/fit/function.h>
#include <lib/fpromise/promise.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/result.h>

#include <list>
#include <unordered_map>

#include <fbl/intrusive_double_list.h>

#include "src/devices/bin/driver_manager/node_group/composite_manager_bridge.h"
#include "src/devices/bin/driver_manager/node_group/node_group_manager.h"
#include "src/devices/bin/driver_manager/v2/composite_assembler.h"
#include "src/devices/bin/driver_manager/v2/composite_manager.h"
#include "src/devices/bin/driver_manager/v2/driver_host.h"
#include "src/devices/bin/driver_manager/v2/node.h"
#include "src/devices/bin/driver_manager/v2/node_group_v2.h"
#include "src/devices/bin/driver_manager/v2/node_removal_tracker.h"
#include "src/devices/bin/driver_manager/v2/node_remover.h"

// Note, all of the logic here assumes we are operating on a single-threaded
// dispatcher. It is not safe to use a multi-threaded dispatcher with this code.

namespace dfv2 {

class DriverRunner : public fidl::WireServer<fuchsia_component_runner::ComponentRunner>,
                     public fidl::WireServer<fuchsia_driver_framework::NodeGroupManager>,
                     public CompositeManagerBridge,
                     public NodeManager,
                     public NodeRemover {
  using LoaderServiceFactory = fit::function<zx::result<fidl::ClientEnd<fuchsia_ldsvc::Loader>>()>;

 public:
  DriverRunner(fidl::ClientEnd<fuchsia_component::Realm> realm,
               fidl::ClientEnd<fuchsia_driver_index::DriverIndex> driver_index,
               inspect::Inspector& inspector, LoaderServiceFactory loader_service_factory,
               async_dispatcher_t* dispatcher);

  // fidl::WireServer<fuchsia_driver_framework::NodeGroupManager>
  void CreateNodeGroup(CreateNodeGroupRequestView request,
                       CreateNodeGroupCompleter::Sync& completer) override;

  // CompositeManagerBridge interface
  void BindNodesForNodeGroups() override;
  void AddNodeGroupToDriverIndex(fuchsia_driver_framework::wire::NodeGroup group,
                                 AddToIndexCallback callback) override;

  fpromise::promise<inspect::Inspector> Inspect() const;
  size_t NumOrphanedNodes() const;
  void PublishComponentRunner(component::OutgoingDirectory& outgoing);
  void PublishNodeGroupManager(component::OutgoingDirectory& outgoing);
  zx::result<> StartRootDriver(std::string_view url);
  std::shared_ptr<Node> root_node();
  // This function schedules a callback to attempt to bind all orphaned nodes against
  // the base drivers.
  void ScheduleBaseDriversBinding();
  // Goes through the orphan list and attempts the bind them again. Sends nodes that are still
  // orphaned back to the orphan list. Tracks the result of the bindings and then when finished
  // uses the result_callback to report the results.
  void TryBindAllOrphans(NodeBindingInfoResultCallback result_callback);

  // Only exposed for testing.
  NodeGroupManager& node_group_manager() { return node_group_manager_; }

  // Create a driver component with `url` against a given `node`.
  zx::result<> StartDriver(Node& node, std::string_view url,
                           fuchsia_driver_index::DriverPackageType package_type);

  // Shutdown hooks called by the shutdown manager
  void ShutdownAllDrivers(fit::callback<void()> callback) override {
    removal_tracker_.set_all_callback(std::move(callback));
    root_node_->Remove(RemovalSet::kAll, &removal_tracker_);
  }
  void ShutdownPkgDrivers(fit::callback<void()> callback) override {
    removal_tracker_.set_pkg_callback(std::move(callback));
    root_node_->Remove(RemovalSet::kPackage, &removal_tracker_);
  }

 private:
  // fidl::WireServer<fuchsia_component_runner::ComponentRunner>
  void Start(StartRequestView request, StartCompleter::Sync& completer) override;
  // NodeManager
  // Attempt to bind `node`.
  // A nullptr for result_tracker is acceptable if the caller doesn't intend to
  // track the results.
  void Bind(Node& node, std::shared_ptr<BindResultTracker> result_tracker) override;

  zx::result<DriverHost*> CreateDriverHost() override;

  // The untracked version of TryBindAllOrphans.
  void TryBindAllOrphansUntracked();

  struct CreateComponentOpts {
    const Node* node = nullptr;
    zx::handle token;
    fidl::ServerEnd<fuchsia_io::Directory> exposed_dir;
  };
  zx::result<> CreateComponent(std::string name, Collection collection, std::string url,
                               CreateComponentOpts opts);

  uint64_t next_driver_host_id_ = 0;
  fidl::WireClient<fuchsia_component::Realm> realm_;
  fidl::WireClient<fuchsia_driver_index::DriverIndex> driver_index_;
  LoaderServiceFactory loader_service_factory_;
  async_dispatcher_t* const dispatcher_;
  std::shared_ptr<Node> root_node_;

  // This is for dfv1 composite devices.
  CompositeDeviceManager composite_device_manager_;

  // This is for dfv2 composites.
  CompositeNodeManager composite_node_manager_;

  // This is for dfv2 node groups.
  NodeGroupManager node_group_manager_;

  NodeRemovalTracker removal_tracker_;

  std::unordered_map<zx_koid_t, Node&> driver_args_;
  fbl::DoublyLinkedList<std::unique_ptr<DriverHostComponent>> driver_hosts_;

  // Orphaned nodes are nodes that have failed to bind to a driver, either
  // because no matching driver could be found, or because the matching driver
  // failed to start.
  std::vector<std::weak_ptr<Node>> orphaned_nodes_;
};

}  // namespace dfv2

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_V2_DRIVER_RUNNER_H_
