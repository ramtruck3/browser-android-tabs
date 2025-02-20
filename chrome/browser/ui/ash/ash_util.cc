// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/ash_util.h"

#include "ash/accelerators/accelerator_controller.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/public/interfaces/ash_window_manager.mojom.h"
#include "ash/public/interfaces/event_properties.mojom.h"
#include "ash/shell.h"
#include "base/macros.h"
#include "components/session_manager/core/session_manager.h"
#include "content/public/common/service_manager_connection.h"
#include "mojo/public/cpp/bindings/type_converter.h"
#include "services/ws/public/cpp/property_type_converters.h"
#include "services/ws/public/mojom/window_manager.mojom.h"
#include "ui/aura/mus/window_mus.h"
#include "ui/aura/mus/window_tree_client.h"
#include "ui/aura/window_event_dispatcher.h"
#include "ui/base/ui_base_features.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/views/mus/mus_client.h"
#include "ui/views/widget/widget.h"
#include "ui/wm/core/shadow_types.h"
#include "ui/wm/core/window_animations.h"

namespace ash_util {

bool IsAcceleratorDeprecated(const ui::Accelerator& accelerator) {
  // When running in mash the browser doesn't handle ash accelerators.
  if (features::IsMultiProcessMash())
    return false;

  return ash::Shell::Get()->accelerator_controller()->IsDeprecated(accelerator);
}

bool WillAshProcessAcceleratorForEvent(const ui::KeyEvent& key_event) {
  return key_event.properties() &&
         key_event.properties()->count(
             ash::mojom::kWillProcessAccelerator_KeyEventProperty);
}

void SetupWidgetInitParamsForContainer(views::Widget::InitParams* params,
                                       int container_id) {
  DCHECK_GE(container_id, ash::kShellWindowId_MinContainer);
  DCHECK_LE(container_id, ash::kShellWindowId_MaxContainer);

  if (features::IsUsingWindowService()) {
    using ws::mojom::WindowManager;
    params->mus_properties[WindowManager::kContainerId_InitProperty] =
        mojo::ConvertTo<std::vector<uint8_t>>(container_id);
    params->mus_properties[WindowManager::kDisplayId_InitProperty] =
        mojo::ConvertTo<std::vector<uint8_t>>(
            display::Screen::GetScreen()->GetPrimaryDisplay().id());
  } else {
    params->parent = ash::Shell::GetContainer(
        ash::Shell::GetPrimaryRootWindow(), container_id);
  }
}

int GetSystemModalDialogContainerId() {
  return session_manager::SessionManager::Get()->session_state() ==
                 session_manager::SessionState::ACTIVE
             ? ash::kShellWindowId_SystemModalContainer
             : ash::kShellWindowId_LockSystemModalContainer;
}

views::Widget::InitParams GetFramelessInitParams() {
  views::Widget::InitParams params;
  params.type = views::Widget::InitParams::TYPE_WINDOW_FRAMELESS;
  params.shadow_type = views::Widget::InitParams::SHADOW_TYPE_DROP;
  params.shadow_elevation = ::wm::kShadowElevationActiveWindow;
  return params;
}

service_manager::Connector* GetServiceManagerConnector() {
  content::ServiceManagerConnection* manager_connection =
      content::ServiceManagerConnection::GetForProcess();
  if (!manager_connection)
    return nullptr;
  return manager_connection->GetConnector();
}

void BounceWindow(aura::Window* window) {
  // Some launcher item controllers may pass |window| on Shell even when Mash is
  // enabled. In that case it should not use WindowMus. See
  // https://crbug.com/950629 and https://crbug.com/887156.
  aura::WindowMus* window_mus = nullptr;
  if (features::IsUsingWindowService())
    window_mus = aura::WindowMus::Get(window->GetRootWindow());
  if (window_mus) {
    views::MusClient::Get()
        ->window_tree_client()
        ->BindWindowManagerInterface<ash::mojom::AshWindowManager>()
        ->BounceWindow(window_mus->server_id());
  } else {
    wm::AnimateWindow(window, wm::WINDOW_ANIMATION_TYPE_BOUNCE);
  }
}

}  // namespace ash_util
