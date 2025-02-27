// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/download/content/factory/download_service_factory_helper.h"

#include "base/files/file_path.h"
#include "build/build_config.h"
#include "components/download/content/factory/navigation_monitor_factory.h"
#include "components/download/content/internal/download_driver_impl.h"
#include "components/download/internal/background_service/client_set.h"
#include "components/download/internal/background_service/config.h"
#include "components/download/internal/background_service/controller_impl.h"
#include "components/download/internal/background_service/download_service_impl.h"
#include "components/download/internal/background_service/download_store.h"
#include "components/download/internal/background_service/empty_file_monitor.h"
#include "components/download/internal/background_service/file_monitor_impl.h"
#include "components/download/internal/background_service/in_memory_download_driver.h"
#include "components/download/internal/background_service/logger_impl.h"
#include "components/download/internal/background_service/model_impl.h"
#include "components/download/internal/background_service/noop_store.h"
#include "components/download/internal/background_service/proto/entry.pb.h"
#include "components/download/internal/background_service/scheduler/scheduler_impl.h"
#include "components/download/public/common/simple_download_manager_coordinator.h"
#include "components/download/public/task/empty_task_scheduler.h"
#include "components/leveldb_proto/content/proto_database_provider_factory.h"
#include "components/leveldb_proto/public/proto_database_provider.h"

#if defined(OS_ANDROID)
#include "components/download/internal/background_service/android/battery_status_listener_android.h"
#include "components/download/network/android/network_status_listener_android.h"
#elif defined(OS_MACOSX)
#include "components/download/internal/background_service/scheduler/battery_status_listener_mac.h"
#include "components/download/network/network_status_listener_mac.h"
#else
#include "components/download/internal/background_service/scheduler/battery_status_listener_impl.h"
#include "components/download/network/network_status_listener_impl.h"
#endif

namespace download {
namespace {
const base::FilePath::CharType kEntryDBStorageDir[] =
    FILE_PATH_LITERAL("EntryDB");
const base::FilePath::CharType kFilesStorageDir[] = FILE_PATH_LITERAL("Files");
}  // namespace

// Helper function to create download service with different implementation
// details.
DownloadService* CreateDownloadServiceInternal(
    SimpleFactoryKey* simple_factory_key,
    std::unique_ptr<DownloadClientMap> clients,
    std::unique_ptr<Configuration> config,
    std::unique_ptr<DownloadDriver> driver,
    std::unique_ptr<Store> store,
    std::unique_ptr<TaskScheduler> task_scheduler,
    std::unique_ptr<FileMonitor> file_monitor,
    network::NetworkConnectionTracker* network_connection_tracker,
    const base::FilePath& files_storage_dir) {
  auto client_set = std::make_unique<ClientSet>(std::move(clients));
  auto model = std::make_unique<ModelImpl>(std::move(store));

// Build platform network/battery status listener.
#if defined(OS_ANDROID)
  auto battery_listener = std::make_unique<BatteryStatusListenerAndroid>(
      config->battery_query_interval);
  auto network_listener = std::make_unique<NetworkStatusListenerAndroid>();
#elif defined(OS_MACOSX)
  auto battery_listener = std::make_unique<BatteryStatusListenerMac>();
  auto network_listener = std::make_unique<NetworkStatusListenerMac>();
#else
  auto battery_listener = std::make_unique<BatteryStatusListenerImpl>(
      config->battery_query_interval);
  auto network_listener =
      std::make_unique<NetworkStatusListenerImpl>(network_connection_tracker);
#endif

  auto device_status_listener = std::make_unique<DeviceStatusListener>(
      config->network_startup_delay, config->network_change_delay,
      std::move(battery_listener), std::move(network_listener));
  NavigationMonitor* navigation_monitor =
      NavigationMonitorFactory::GetForKey(simple_factory_key);
  auto scheduler = std::make_unique<SchedulerImpl>(
      task_scheduler.get(), config.get(), client_set.get());
  auto logger = std::make_unique<LoggerImpl>();
  auto controller = std::make_unique<ControllerImpl>(
      config.get(), logger.get(), std::move(client_set), std::move(driver),
      std::move(model), std::move(device_status_listener), navigation_monitor,
      std::move(scheduler), std::move(task_scheduler), std::move(file_monitor),
      files_storage_dir);
  logger->SetLogSource(controller.get());

  return new DownloadServiceImpl(std::move(config), std::move(logger),
                                 std::move(controller));
}

// Create download service for normal profile.
DownloadService* BuildDownloadService(
    content::BrowserContext* browser_context,
    SimpleFactoryKey* simple_factory_key,
    PrefService* prefs,
    std::unique_ptr<DownloadClientMap> clients,
    network::NetworkConnectionTracker* network_connection_tracker,
    const base::FilePath& storage_dir,
    SimpleDownloadManagerCoordinator* download_manager_coordinator,
    const scoped_refptr<base::SequencedTaskRunner>& background_task_runner,
    std::unique_ptr<TaskScheduler> task_scheduler) {
  auto config = Configuration::CreateFromFinch();

  auto driver = std::make_unique<DownloadDriverImpl>(
      content::BrowserContext::GetDownloadManager(browser_context),
      download_manager_coordinator);

  auto entry_db_storage_dir = storage_dir.Append(kEntryDBStorageDir);

  leveldb_proto::ProtoDatabaseProvider* db_provider =
      leveldb_proto::ProtoDatabaseProviderFactory::GetForKey(
          simple_factory_key);
  auto entry_db = db_provider->GetDB<protodb::Entry>(
      leveldb_proto::ProtoDbType::DOWNLOAD_STORE, entry_db_storage_dir,
      background_task_runner);
  auto store = std::make_unique<DownloadStore>(std::move(entry_db));

  auto files_storage_dir = storage_dir.Append(kFilesStorageDir);
  auto file_monitor = std::make_unique<FileMonitorImpl>(
      files_storage_dir, background_task_runner, config->file_keep_alive_time);

  return CreateDownloadServiceInternal(
      simple_factory_key, std::move(clients), std::move(config),
      std::move(driver), std::move(store), std::move(task_scheduler),
      std::move(file_monitor), network_connection_tracker, files_storage_dir);
}

// Create download service for incognito mode without any database or file IO.
DownloadService* BuildInMemoryDownloadService(
    SimpleFactoryKey* simple_factory_key,
    std::unique_ptr<DownloadClientMap> clients,
    network::NetworkConnectionTracker* network_connection_tracker,
    const base::FilePath& storage_dir,
    BlobTaskProxy::BlobContextGetter blob_context_getter,
    scoped_refptr<base::SingleThreadTaskRunner> io_task_runner,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory) {
  auto config = Configuration::CreateFromFinch();
  auto download_factory = std::make_unique<InMemoryDownloadFactory>(
      url_loader_factory.get(), blob_context_getter, io_task_runner);
  auto driver =
      std::make_unique<InMemoryDownloadDriver>(std::move(download_factory));
  auto store = std::make_unique<NoopStore>();
  auto task_scheduler = std::make_unique<EmptyTaskScheduler>();

  // TODO(xingliu): Remove |files_storage_dir| and |storage_dir| for incognito
  // mode. See https://crbug.com/810202.
  auto files_storage_dir = storage_dir.Append(kFilesStorageDir);
  auto file_monitor = std::make_unique<EmptyFileMonitor>();

  return CreateDownloadServiceInternal(
      simple_factory_key, std::move(clients), std::move(config),
      std::move(driver), std::move(store), std::move(task_scheduler),
      std::move(file_monitor), network_connection_tracker, files_storage_dir);
}

}  // namespace download
