// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/app_list/views/search_result_view.h"

#include <algorithm>
#include <utility>

#include "ash/app_list/app_list_metrics.h"
#include "ash/app_list/app_list_view_delegate.h"
#include "ash/app_list/model/search/search_result.h"
#include "ash/app_list/views/app_list_main_view.h"
#include "ash/app_list/views/remove_query_confirmation_dialog.h"
#include "ash/app_list/views/search_box_view.h"
#include "ash/app_list/views/search_result_actions_view.h"
#include "ash/app_list/views/search_result_list_view.h"
#include "ash/public/cpp/app_list/app_list_config.h"
#include "ash/public/cpp/app_list/app_list_switches.h"
#include "ash/public/interfaces/app_list.mojom.h"
#include "base/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/font.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/render_text.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/progress_bar.h"

namespace app_list {

namespace {

constexpr int kPreferredWidth = 640;
constexpr int kPreferredHeight = 48;
constexpr int kPreferredIconViewWidth = 56;
constexpr int kTextTrailPadding = 16;
// Extra margin at the right of the rightmost action icon.
constexpr int kActionButtonRightMargin = 8;
// Text line height in the search result.
constexpr int kTitleLineHeight = 20;
constexpr int kDetailsLineHeight = 16;

// Matched text color.
constexpr SkColor kMatchedTextColor = gfx::kGoogleGrey900;
// Default text color.
constexpr SkColor kDefaultTextColor = gfx::kGoogleGrey700;
// URL color.
constexpr SkColor kUrlColor = gfx::kGoogleBlue600;
// Row selected color, Google Grey 8%.
constexpr SkColor kRowHighlightedColor = SkColorSetA(gfx::kGoogleGrey900, 0x14);
// Search result border color.
constexpr SkColor kResultBorderColor = SkColorSetARGB(0xFF, 0xE5, 0xE5, 0xE5);

// Delta applied to font size of all AppListSearchResult titles.
constexpr int kSearchResultTitleTextSizeDelta = 2;

}  // namespace

// static
const char SearchResultView::kViewClassName[] = "ui/app_list/SearchResultView";

SearchResultView::SearchResultView(SearchResultListView* list_view,
                                   AppListViewDelegate* view_delegate)
    : list_view_(list_view),
      view_delegate_(view_delegate),
      icon_(new views::ImageView),
      display_icon_(new views::ImageView),
      badge_icon_(new views::ImageView),
      actions_view_(new SearchResultActionsView(this)),
      progress_bar_(new views::ProgressBar),
      weak_ptr_factory_(this) {
  SetFocusBehavior(FocusBehavior::ALWAYS);
  icon_->set_can_process_events_within_subtree(false);
  display_icon_->set_can_process_events_within_subtree(false);
  SetDisplayIcon(gfx::ImageSkia());
  badge_icon_->set_can_process_events_within_subtree(false);

  AddChildView(icon_);
  AddChildView(display_icon_);
  AddChildView(badge_icon_);
  AddChildView(actions_view_);
  AddChildView(progress_bar_);
  set_context_menu_controller(this);
  set_notify_enter_exit_on_child(true);
}

SearchResultView::~SearchResultView() {
  ClearResult();
}

void SearchResultView::OnResultChanged() {
  OnMetadataChanged();
  UpdateTitleText();
  UpdateDetailsText();
  OnIsInstallingChanged();
  OnPercentDownloadedChanged();
  SchedulePaint();
}

void SearchResultView::ClearSelectedAction() {
  actions_view_->SetSelectedAction(-1);
}

void SearchResultView::UpdateTitleText() {
  if (!result() || result()->title().empty())
    title_text_.reset();
  else
    CreateTitleRenderText();

  UpdateAccessibleName();
}

void SearchResultView::UpdateDetailsText() {
  if (!result() || result()->details().empty())
    details_text_.reset();
  else
    CreateDetailsRenderText();

  UpdateAccessibleName();
}

base::string16 SearchResultView::ComputeAccessibleName() const {
  if (!result())
    return base::string16();

  base::string16 accessible_name = result()->title();
  if (!result()->title().empty() && !result()->details().empty())
    accessible_name += base::ASCIIToUTF16(", ");
  accessible_name += result()->details();

  return accessible_name;
}

void SearchResultView::UpdateAccessibleName() {
  SetAccessibleName(ComputeAccessibleName());
}

void SearchResultView::CreateTitleRenderText() {
  auto render_text = gfx::RenderText::CreateHarfBuzzInstance();
  render_text->SetText(result()->title());
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  render_text->SetFontList(
      rb.GetFontList(AppListConfig::instance().search_result_title_font_style())
          .DeriveWithSizeDelta(kSearchResultTitleTextSizeDelta));
  // When result is an omnibox non-url search, the matched tag indicates
  // proposed query. For all other cases, the matched tag indicates typed search
  // query.
  render_text->SetColor(result()->is_omnibox_search() ? kDefaultTextColor
                                                      : kMatchedTextColor);
  const SearchResult::Tags& tags = result()->title_tags();
  for (const auto& tag : tags) {
    if (tag.styles & SearchResult::Tag::URL) {
      render_text->ApplyColor(kUrlColor, tag.range);
    } else if (tag.styles & SearchResult::Tag::MATCH) {
      render_text->ApplyColor(
          result()->is_omnibox_search() ? kMatchedTextColor : kDefaultTextColor,
          tag.range);
    }
  }
  title_text_ = std::move(render_text);
}

void SearchResultView::CreateDetailsRenderText() {
  // Ensures single line row for omnibox non-url search result.
  if (result()->is_omnibox_search()) {
    details_text_.reset();
    return;
  }
  auto render_text = gfx::RenderText::CreateHarfBuzzInstance();
  render_text->SetText(result()->details());
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  render_text->SetFontList(rb.GetFontList(ui::ResourceBundle::BaseFont));
  render_text->SetColor(kDefaultTextColor);
  const SearchResult::Tags& tags = result()->details_tags();
  for (const auto& tag : tags) {
    if (tag.styles & SearchResult::Tag::URL)
      render_text->ApplyColor(kUrlColor, tag.range);
  }
  details_text_ = std::move(render_text);
}

void SearchResultView::OnQueryRemovalAccepted(bool accepted, int event_flags) {
  if (accepted) {
    list_view_->SearchResultActionActivated(
        this, ash::OmniBoxZeroStateAction::kRemoveSuggestion, event_flags);
  }

  if (confirm_remove_by_long_press_) {
    confirm_remove_by_long_press_ = false;
    SetBackgroundHighlighted(false);
  }

  RecordZeroStateSearchResultRemovalHistogram(
      accepted ? ZeroStateSearchResutRemovalConfirmation::kRemovalConfirmed
               : ZeroStateSearchResutRemovalConfirmation::kRemovalCanceled);
}

const char* SearchResultView::GetClassName() const {
  return kViewClassName;
}

gfx::Size SearchResultView::CalculatePreferredSize() const {
  return gfx::Size(kPreferredWidth, kPreferredHeight);
}

void SearchResultView::Layout() {
  gfx::Rect rect(GetContentsBounds());
  if (rect.IsEmpty())
    return;

  gfx::Rect icon_bounds(rect);

  const bool has_display_icon = !display_icon_->GetImage().isNull();
  views::ImageView* icon = has_display_icon ? display_icon_ : icon_;
  const int left_right_padding =
      (kPreferredIconViewWidth - icon->GetImage().width()) / 2;
  const int top_bottom_padding =
      (rect.height() - icon->GetImage().height()) / 2;
  icon_bounds.set_width(kPreferredIconViewWidth);
  icon_bounds.Inset(left_right_padding, top_bottom_padding);
  icon_bounds.Intersect(rect);
  icon->SetBoundsRect(icon_bounds);

  gfx::Rect badge_icon_bounds;

  const int badge_icon_dimension =
      AppListConfig::instance().search_list_badge_icon_dimension();
  badge_icon_bounds = gfx::Rect(icon_bounds.right() - badge_icon_dimension / 2,
                                icon_bounds.bottom() - badge_icon_dimension / 2,
                                badge_icon_dimension, badge_icon_dimension);

  badge_icon_bounds.Intersect(rect);
  badge_icon_->SetBoundsRect(badge_icon_bounds);

  const int max_actions_width =
      (rect.right() - kActionButtonRightMargin - icon_bounds.right()) / 2;
  int actions_width =
      std::min(max_actions_width, actions_view_->GetPreferredSize().width());

  gfx::Rect actions_bounds(rect);
  actions_bounds.set_x(rect.right() - kActionButtonRightMargin - actions_width);
  actions_bounds.set_width(actions_width);
  actions_view_->SetBoundsRect(actions_bounds);

  const int progress_width = rect.width() / 5;
  const int progress_height = progress_bar_->GetPreferredSize().height();
  const gfx::Rect progress_bounds(
      rect.right() - kActionButtonRightMargin - progress_width,
      rect.y() + (rect.height() - progress_height) / 2, progress_width,
      progress_height);
  progress_bar_->SetBoundsRect(progress_bounds);
}

bool SearchResultView::OnKeyPressed(const ui::KeyEvent& event) {
  // |result()| could be NULL when result list is changing.
  if (!result())
    return false;

  switch (event.key_code()) {
    case ui::VKEY_RETURN: {
      int selected = actions_view_->selected_action();
      if (actions_view_->IsValidActionIndex(selected)) {
        OnSearchResultActionActivated(selected, event.flags());
      } else {
        list_view_->SearchResultActivated(this, event.flags());
      }
      return true;
    }
    case ui::VKEY_UP:
    case ui::VKEY_DOWN: {
      if (!actions_view_->children().empty()) {
        return list_view_->HandleVerticalFocusMovement(
            this, event.key_code() == ui::VKEY_UP);
      }
      break;
    }
    default:
      break;
  }

  return false;
}

void SearchResultView::PaintButtonContents(gfx::Canvas* canvas) {
  gfx::Rect rect(GetContentsBounds());
  if (rect.IsEmpty())
    return;

  gfx::Rect content_rect(rect);
  gfx::Rect text_bounds(rect);
  text_bounds.set_x(kPreferredIconViewWidth);
  if (actions_view_->visible()) {
    text_bounds.set_width(
        rect.width() - kPreferredIconViewWidth - kTextTrailPadding -
        actions_view_->bounds().width() -
        (actions_view_->children().empty() ? 0 : kActionButtonRightMargin));
  } else {
    text_bounds.set_width(rect.width() - kPreferredIconViewWidth -
                          kTextTrailPadding - progress_bar_->bounds().width() -
                          kActionButtonRightMargin);
  }
  text_bounds.set_x(
      GetMirroredXWithWidthInView(text_bounds.x(), text_bounds.width()));

  // Set solid color background to avoid broken text. See crbug.com/746563.
  // This should be drawn before selected color which is semi-transparent.
  canvas->FillRect(text_bounds,
                   AppListConfig::instance().card_background_color());

  // Possibly call FillRect a second time (these colours are partially
  // transparent, so the previous FillRect is not redundant).
  if (background_highlighted())
    canvas->FillRect(content_rect, kRowHighlightedColor);

  gfx::Rect border_bottom = gfx::SubtractRects(rect, content_rect);
  canvas->FillRect(border_bottom, kResultBorderColor);

  if (title_text_ && details_text_) {
    gfx::Size title_size(text_bounds.width(), kTitleLineHeight);
    gfx::Size details_size(text_bounds.width(), kDetailsLineHeight);
    int total_height = title_size.height() + details_size.height();
    int y = text_bounds.y() + (text_bounds.height() - total_height) / 2;

    title_text_->SetDisplayRect(
        gfx::Rect(gfx::Point(text_bounds.x(), y), title_size));
    title_text_->Draw(canvas);

    y += title_size.height();
    details_text_->SetDisplayRect(
        gfx::Rect(gfx::Point(text_bounds.x(), y), details_size));
    details_text_->Draw(canvas);
  } else if (title_text_) {
    gfx::Size title_size(text_bounds.width(),
                         title_text_->GetStringSize().height());
    gfx::Rect centered_title_rect(text_bounds);
    centered_title_rect.ClampToCenteredSize(title_size);
    title_text_->SetDisplayRect(centered_title_rect);
    title_text_->Draw(canvas);
  }
}

void SearchResultView::OnFocus() {
  ScrollRectToVisible(GetLocalBounds());
  SetBackgroundHighlighted(true);
  selected_ = true;
  actions_view_->UpdateButtonsOnStateChanged();
}

void SearchResultView::OnBlur() {
  SetBackgroundHighlighted(false);
  selected_ = false;
  actions_view_->UpdateButtonsOnStateChanged();
}

void SearchResultView::OnMouseEntered(const ui::MouseEvent& event) {
  actions_view_->UpdateButtonsOnStateChanged();
}

void SearchResultView::OnMouseExited(const ui::MouseEvent& event) {
  actions_view_->UpdateButtonsOnStateChanged();
}

void SearchResultView::GetAccessibleNodeData(ui::AXNodeData* node_data) {
  if (!visible())
    return;

  // This is a work around to deal with the nested button case(append and remove
  // button are child button of SearchResultView), which is not supported by
  // ChromeVox. see details in crbug.com/924776.
  // We change the role of the parent view SearchResultView to kGenericContainer
  // i.e., not a kButton anymore.
  node_data->role = ax::mojom::Role::kGenericContainer;
  node_data->AddState(ax::mojom::State::kFocusable);
  node_data->SetDefaultActionVerb(ax::mojom::DefaultActionVerb::kClick);
  node_data->SetName(GetAccessibleName());
}

void SearchResultView::VisibilityChanged(View* starting_from, bool is_visible) {
  NotifyAccessibilityEvent(ax::mojom::Event::kLayoutComplete, true);
}

void SearchResultView::OnGestureEvent(ui::GestureEvent* event) {
  switch (event->type()) {
    case ui::ET_GESTURE_LONG_PRESS:
      if (actions_view_->IsValidActionIndex(
              ash::OmniBoxZeroStateAction::kRemoveSuggestion)) {
        ScrollRectToVisible(GetLocalBounds());
        NotifyAccessibilityEvent(ax::mojom::Event::kSelection, true);
        SetBackgroundHighlighted(true);
        confirm_remove_by_long_press_ = true;
        OnSearchResultActionActivated(
            ash::OmniBoxZeroStateAction::kRemoveSuggestion, event->flags());
        event->SetHandled();
      }
      break;
    default:
      break;
  }
  if (!event->handled())
    Button::OnGestureEvent(event);
}

void SearchResultView::ButtonPressed(views::Button* sender,
                                     const ui::Event& event) {
  DCHECK(sender == this);
  list_view_->SearchResultActivated(this, event.flags());
}

void SearchResultView::OnMetadataChanged() {
  // Updates |icon_|.
  // Note: this might leave the view with an old icon. But it is needed to avoid
  // flash when a SearchResult's icon is loaded asynchronously. In this case, it
  // looks nicer to keep the stale icon for a little while on screen instead of
  // clearing it out. It should work correctly as long as the SearchResult does
  // not forget to SetIcon when it's ready.
  const gfx::ImageSkia icon(result() ? result()->icon() : gfx::ImageSkia());
  if (!icon.isNull())
    SetIconImage(icon, icon_,
                 AppListConfig::instance().search_list_icon_dimension());

  // Updates |badge_icon_|.
  const gfx::ImageSkia badge_icon(result() ? result()->badge_icon()
                                           : gfx::ImageSkia());
  if (badge_icon.isNull()) {
    badge_icon_->SetVisible(false);
  } else {
    SetIconImage(badge_icon, badge_icon_,
                 AppListConfig::instance().search_list_badge_icon_dimension());
    badge_icon_->SetVisible(true);
  }

  // Updates |actions_view_|.
  actions_view_->SetActions(result() ? result()->actions()
                                     : SearchResult::Actions());
}

void SearchResultView::SetIconImage(const gfx::ImageSkia& source,
                                    views::ImageView* const icon,
                                    const int icon_dimension) {
  gfx::ImageSkia image(source);
  image = gfx::ImageSkiaOperations::CreateResizedImage(
      source, skia::ImageOperations::RESIZE_BEST,
      gfx::Size(icon_dimension, icon_dimension));
  icon->SetImage(image);
}

void SearchResultView::OnIsInstallingChanged() {
  const bool is_installing = result() && result()->is_installing();
  actions_view_->SetVisible(!is_installing);
  progress_bar_->SetVisible(is_installing);
}

void SearchResultView::OnPercentDownloadedChanged() {
  progress_bar_->SetValue(result() ? result()->percent_downloaded() / 100.0
                                   : 0);
}

void SearchResultView::OnItemInstalled() {
  list_view_->OnSearchResultInstalled(this);
}

void SearchResultView::OnSearchResultActionActivated(size_t index,
                                                     int event_flags) {
  // |result()| could be NULL when result list is changing.
  if (!result())
    return;

  DCHECK_LT(index, result()->actions().size());

  if (result()->is_omnibox_search()) {
    ash::OmniBoxZeroStateAction button_action =
        ash::GetOmniBoxZeroStateAction(index);

    if (button_action == ash::OmniBoxZeroStateAction::kRemoveSuggestion) {
      RecordZeroStateSearchResultUserActionHistogram(
          ZeroStateSearchResultUserActionType::kRemoveResult);
      RemoveQueryConfirmationDialog* dialog = new RemoveQueryConfirmationDialog(
          base::BindOnce(&SearchResultView::OnQueryRemovalAccepted,
                         weak_ptr_factory_.GetWeakPtr()),
          event_flags, list_view_->app_list_main_view()->contents_view());

      dialog->Show(GetWidget()->GetNativeWindow());
    } else if (button_action ==
               ash::OmniBoxZeroStateAction::kAppendSuggestion) {
      RecordZeroStateSearchResultUserActionHistogram(
          ZeroStateSearchResultUserActionType::kAppendResult);
      list_view_->SearchResultActionActivated(this, index, event_flags);
    }
  }
}

bool SearchResultView::IsSearchResultHoveredOrSelected() {
  return IsMouseHovered() || selected();
}

void SearchResultView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::MenuSourceType source_type) {
  // |result()| could be NULL when result list is changing.
  if (!result())
    return;

  view_delegate_->GetSearchResultContextMenuModel(
      result()->id(), base::BindOnce(&SearchResultView::OnGetContextMenu,
                                     weak_ptr_factory_.GetWeakPtr(), source,
                                     point, source_type));
}

void SearchResultView::OnGetContextMenu(
    views::View* source,
    const gfx::Point& point,
    ui::MenuSourceType source_type,
    std::vector<ash::mojom::MenuItemPtr> menu) {
  if (menu.empty() || context_menu_->IsShowingMenu())
    return;

  context_menu_ = std::make_unique<AppListMenuModelAdapter>(
      std::string(), GetWidget(), source_type, this,
      AppListMenuModelAdapter::SEARCH_RESULT, base::OnceClosure(),
      view_delegate_->GetSearchModel()->tablet_mode());
  context_menu_->Build(std::move(menu));
  context_menu_->Run(gfx::Rect(point, gfx::Size()),
                     views::MenuAnchorPosition::kTopLeft,
                     views::MenuRunner::HAS_MNEMONICS);
  source->RequestFocus();
}

void SearchResultView::ExecuteCommand(int command_id, int event_flags) {
  if (result()) {
    view_delegate_->SearchResultContextMenuItemSelected(
        result()->id(), command_id, event_flags,
        ash::mojom::AppListLaunchType::kSearchResult);
  }
}

void SearchResultView::SetDisplayIcon(const gfx::ImageSkia& source) {
  display_icon_->SetImage(source);
  display_icon_->SetVisible(!source.isNull());
  icon_->SetVisible(source.isNull());
}

}  // namespace app_list
