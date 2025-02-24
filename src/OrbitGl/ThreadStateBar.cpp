// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ThreadStateBar.h"

#include <absl/strings/str_format.h>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "App.h"
#include "AsyncTrack.h"
#include "CaptureViewElement.h"
#include "ClientData/CallstackType.h"
#include "ClientData/CaptureData.h"
#include "ClientData/ThreadStateSliceInfo.h"
#include "ClientProtos/capture_data.pb.h"
#include "CoreMath.h"
#include "DisplayFormats/DisplayFormats.h"
#include "Geometry.h"
#include "GlCanvas.h"
#include "GlUtils.h"
#include "GrpcProtos/capture.pb.h"
#include "OrbitBase/Logging.h"
#include "PrimitiveAssembler.h"
#include "ThreadBar.h"
#include "Viewport.h"

using EventResult = AsyncTrack::EventResult;
using orbit_client_data::ThreadID;
using orbit_client_data::ThreadStateSliceInfo;
using orbit_grpc_protos::ThreadStateSlice;

namespace orbit_gl {

ThreadStateBar::ThreadStateBar(CaptureViewElement* parent, OrbitApp* app,
                               const orbit_gl::TimelineInfoInterface* timeline_info,
                               orbit_gl::Viewport* viewport, TimeGraphLayout* layout,
                               const orbit_client_data::ModuleManager* module_manager,
                               const orbit_client_data::CaptureData* capture_data,
                               ThreadID thread_id)
    : ThreadBar(parent, app, timeline_info, viewport, layout, module_manager, capture_data,
                thread_id, "ThreadState") {}

bool ThreadStateBar::IsEmpty() const {
  return capture_data_ == nullptr || !capture_data_->HasThreadStatesForThread(GetThreadId());
}

void ThreadStateBar::DoDraw(PrimitiveAssembler& primitive_assembler, TextRenderer& text_renderer,
                            const DrawContext& draw_context) {
  ThreadBar::DoDraw(primitive_assembler, text_renderer, draw_context);

  // Similarly to CallstackThreadBar::DoDraw, the thread state slices don't respond to clicks, but
  // have a tooltip. For picking, we want to draw the event bar over them if handling a click, and
  // underneath otherwise. This simulates "click-through" behavior.
  float thread_state_bar_z = draw_context.picking_mode == PickingMode::kClick
                                 ? GlCanvas::kZValueEventBarPicking
                                 : GlCanvas::kZValueEventBar;

  // Draw a transparent track just for clicking.
  Quad box = MakeBox(GetPos(), Vec2(GetWidth(), GetHeight()));
  static const Color kTransparent{0, 0, 0, 0};
  primitive_assembler.AddBox(box, thread_state_bar_z, kTransparent, shared_from_this());
}

static Color GetThreadStateColor(ThreadStateSlice::ThreadState state) {
  static const Color kGreen500{76, 175, 80, 255};
  static const Color kBlue500{33, 150, 243, 255};
  static const Color kGray600{117, 117, 117, 255};
  static const Color kOrange500{255, 152, 0, 255};
  static const Color kRed500{244, 67, 54, 255};
  static const Color kPurple500{156, 39, 176, 255};
  static const Color kBlack{0, 0, 0, 255};
  static const Color kBrown500{121, 85, 72, 255};

  switch (state) {
    case ThreadStateSlice::kRunning:
      return kGreen500;
    case ThreadStateSlice::kRunnable:
      return kBlue500;
    case ThreadStateSlice::kInterruptibleSleep:
      return kGray600;
    case ThreadStateSlice::kUninterruptibleSleep:
      return kOrange500;
    case ThreadStateSlice::kStopped:
      return kRed500;
    case ThreadStateSlice::kTraced:
      return kPurple500;
    case ThreadStateSlice::kDead:
      [[fallthrough]];
    case ThreadStateSlice::kZombie:
      return kBlack;
    case ThreadStateSlice::kParked:
      [[fallthrough]];
    case ThreadStateSlice::kIdle:
      return kBrown500;
    default:
      ORBIT_UNREACHABLE();
  }
}

[[nodiscard]] static std::string GetWakeupReason(ThreadStateSliceInfo::WakeupReason reason) {
  switch (reason) {
    case ThreadStateSliceInfo::WakeupReason::kNotApplicable:
      ORBIT_UNREACHABLE();
    case ThreadStateSliceInfo::WakeupReason::kUnblocked:
      return "unblocked";
    case ThreadStateSliceInfo::WakeupReason::kCreated:
      return "created";
  }
  ORBIT_UNREACHABLE();
}

static std::string GetThreadStateName(ThreadStateSlice::ThreadState state) {
  switch (state) {
    case ThreadStateSlice::kRunning:
      return "Running";
    case ThreadStateSlice::kRunnable:
      return "Runnable";
    case ThreadStateSlice::kInterruptibleSleep:
      return "Interruptible sleep";
    case ThreadStateSlice::kUninterruptibleSleep:
      return "Uninterruptible sleep";
    case ThreadStateSlice::kStopped:
      return "Stopped";
    case ThreadStateSlice::kTraced:
      return "Traced";
    case ThreadStateSlice::kDead:
      return "Dead";
    case ThreadStateSlice::kZombie:
      return "Zombie";
    case ThreadStateSlice::kParked:
      return "Parked";
    case ThreadStateSlice::kIdle:
      return "Idle";
    default:
      ORBIT_UNREACHABLE();
  }
}

static std::string GetThreadStateDescription(ThreadStateSlice::ThreadState state) {
  switch (state) {
    case ThreadStateSlice::kRunning:
      return "The thread is currently scheduled on the CPU.";
    case ThreadStateSlice::kRunnable:
      return "The thread is ready to use the CPU, but is currently not scheduled.";
    case ThreadStateSlice::kInterruptibleSleep:
      return "The thread is waiting for a resource to become available or for an event to happen.";
    case ThreadStateSlice::kUninterruptibleSleep:
      return "The thread performed a specific system call that cannot be interrupted by any signal "
             "and is waiting for the call to complete.";
    case ThreadStateSlice::kStopped:
      return "The execution of the thread was suspended with the SIGSTOP signal.";
    case ThreadStateSlice::kTraced:
      return "The thread is stopped because a tracer (for example, a debugger) is attached to it.";
    case ThreadStateSlice::kDead:
      [[fallthrough]];
    case ThreadStateSlice::kZombie:
      return "The thread has exited.";
    case ThreadStateSlice::kParked:
      return "Parked kernel thread.";
    case ThreadStateSlice::kIdle:
      return "Idle kernel thread.";
    default:
      ORBIT_UNREACHABLE();
  }
}

std::string ThreadStateBar::GetThreadStateSliceTooltip(PrimitiveAssembler& primitive_assembler,
                                                       PickingId id) const {
  const PickingUserData* user_data = primitive_assembler.GetUserData(id);
  if (user_data == nullptr || user_data->custom_data_ == nullptr) {
    return "";
  }

  const auto* thread_state_slice =
      static_cast<const ThreadStateSliceInfo*>(user_data->custom_data_);

  std::string tooltip = absl::StrFormat(
      "<b>%s</b><br/>"
      "<i>Thread state</i><br/>"
      "<br/>"
      "%s<br/>",
      GetThreadStateName(thread_state_slice->thread_state()),
      GetThreadStateDescription(thread_state_slice->thread_state()));

  if (thread_state_slice->wakeup_reason() != ThreadStateSliceInfo::WakeupReason::kNotApplicable) {
    std::string reason = GetWakeupReason(thread_state_slice->wakeup_reason());
    std::string thread_name = capture_data_->GetThreadName(thread_state_slice->wakeup_tid());
    std::string process_name = capture_data_->GetThreadName(thread_state_slice->wakeup_pid());

    tooltip += absl::StrFormat(
        "<br/>"
        "<b>Was %s by process:</b> %s [%d]"
        "<br/>"
        "<b>Was %s by thread:</b> %s [%d]",
        reason, process_name, thread_state_slice->wakeup_pid(), reason, thread_name,
        thread_state_slice->wakeup_tid());
  }

  uint64_t begin_ns = thread_state_slice->begin_timestamp_ns();
  uint64_t end_ns = thread_state_slice->end_timestamp_ns();
  tooltip += absl::StrFormat(
      "<br/>"
      "<b>Time:</b> %s",
      orbit_display_formats::GetDisplayTime(TicksToDuration(begin_ns, end_ns)));

  return tooltip;
}

void ThreadStateBar::DrawThreadStateSliceOutline(PrimitiveAssembler& primitive_assembler,
                                                 const ThreadStateSliceInfo& slice,
                                                 const Color& outline_color) const {
  float left_x = timeline_info_->GetWorldFromTick(slice.begin_timestamp_ns());
  float right_x = timeline_info_->GetWorldFromTick(slice.end_timestamp_ns());
  float top_y = GetPos()[1];
  float bottom_y = GetPos()[1] + GetHeight();
  std::array<Vec2, 4> outline_points{
      Vec2{left_x, top_y},
      Vec2{right_x, top_y},
      Vec2{right_x, bottom_y},
      Vec2{left_x, bottom_y},
  };
  Quad outline{outline_points};
  primitive_assembler.AddQuadBorder(outline, GlCanvas::kZValueBoxBorder, outline_color);
}

void ThreadStateBar::DoUpdatePrimitives(PrimitiveAssembler& primitive_assembler,
                                        TextRenderer& text_renderer, uint64_t min_tick,
                                        uint64_t max_tick, PickingMode picking_mode) {
  ORBIT_SCOPE_WITH_COLOR("ThreadStateBar::DoUpdatePrimitives", kOrbitColorTeal);
  ThreadBar::DoUpdatePrimitives(primitive_assembler, text_renderer, min_tick, max_tick,
                                picking_mode);

  const auto time_window_ns = static_cast<uint64_t>(1000 * timeline_info_->GetTimeWindowUs());
  const uint64_t pixel_delta_ns = time_window_ns / viewport_->WorldToScreen(GetSize())[0];
  const uint64_t min_time_graph_ns = timeline_info_->GetTickFromUs(timeline_info_->GetMinTimeUs());
  const float pixel_width_in_world_coords = viewport_->ScreenToWorld({1, 0})[0];

  uint64_t ignore_until_ns = 0;

  ORBIT_CHECK(capture_data_ != nullptr);
  capture_data_->ForEachThreadStateSliceIntersectingTimeRange(
      GetThreadId(), min_tick, max_tick, [&](const ThreadStateSliceInfo& slice) {
        if (slice.end_timestamp_ns() <= ignore_until_ns) {
          // Reduce overdraw by not drawing slices whose entire width would only draw over a
          // previous slice. Similar to TimerTrack::UpdatePrimitives.
          return;
        }

        const float x0 = timeline_info_->GetWorldFromTick(slice.begin_timestamp_ns());
        const float x1 = timeline_info_->GetWorldFromTick(slice.end_timestamp_ns());
        const float width = x1 - x0;

        const Vec2 pos{x0, GetPos()[1]};
        const Vec2 size{width, GetHeight()};

        if (slice == app_->hovered_thread_state_slice() ||
            slice == app_->selected_thread_state_slice()) {
          int outline_transparency = slice == app_->selected_thread_state_slice() ? 255 : 64;
          const Color outline_color = Color(255, 255, 255, outline_transparency);
          DrawThreadStateSliceOutline(primitive_assembler, slice, outline_color);
        }

        const Color color = GetThreadStateColor(slice.thread_state());

        auto user_data = std::make_unique<PickingUserData>(nullptr, [&](PickingId id) {
          return GetThreadStateSliceTooltip(primitive_assembler, id);
        });
        user_data->custom_data_ = &slice;

        if (slice.end_timestamp_ns() - slice.begin_timestamp_ns() > pixel_delta_ns) {
          Quad box = MakeBox(pos, size);
          primitive_assembler.AddBox(box, GlCanvas::kZValueEvent, color, std::move(user_data));
        } else {
          // Make this slice cover an entire pixel and don't draw subsequent slices that would
          // coincide with the same pixel.
          // Use AddBox instead of AddVerticalLine as otherwise the tops of Boxes and lines wouldn't
          // be properly aligned.
          Quad box = MakeBox(pos, {pixel_width_in_world_coords, size[1]});
          primitive_assembler.AddBox(box, GlCanvas::kZValueEvent, color, std::move(user_data));

          if (pixel_delta_ns != 0) {
            ignore_until_ns =
                min_time_graph_ns +
                (slice.begin_timestamp_ns() - min_time_graph_ns) / pixel_delta_ns * pixel_delta_ns +
                pixel_delta_ns;
          }
        }
      });
}

std::optional<ThreadStateSliceInfo> ThreadStateBar::FindSliceFromWorldCoords(
    const Vec2& pos) const {
  uint64_t timestamp = timeline_info_->GetTickFromWorld(pos[0]);
  std::optional<ThreadStateSliceInfo> slice =
      capture_data_->FindThreadStateSliceInfoFromTimestamp(GetThreadId(), timestamp);
  return slice;
}

void ThreadStateBar::OnPick(int x, int y) {
  ThreadBar::OnPick(x, y);
  app_->set_selected_thread_id(GetThreadId());

  Vec2 world_coords = viewport_->ScreenToWorld(Vec2i(x, y));
  std::optional<ThreadStateSliceInfo> clicked_slice = FindSliceFromWorldCoords(world_coords);
  app_->set_selected_thread_state_slice(clicked_slice);
}

EventResult ThreadStateBar::OnMouseMove(const Vec2& mouse_pos) {
  EventResult event_result = CaptureViewElement::OnMouseMove(mouse_pos);
  std::optional<ThreadStateSliceInfo> hovered_slice = FindSliceFromWorldCoords(mouse_pos);
  app_->set_hovered_thread_state_slice(hovered_slice);
  return event_result;
}

EventResult ThreadStateBar::OnMouseLeave() {
  EventResult event_result = CaptureViewElement::OnMouseLeave();
  app_->set_hovered_thread_state_slice(std::nullopt);
  return event_result;
}

}  // namespace orbit_gl
