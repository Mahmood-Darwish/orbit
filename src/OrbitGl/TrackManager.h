// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ORBIT_GL_TRACK_MANAGER_H_
#define ORBIT_GL_TRACK_MANAGER_H_

#include <absl/container/flat_hash_map.h>
#include <stdint.h>
#include <stdlib.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "AsyncTrack.h"
#include "CGroupAndProcessMemoryTrack.h"
#include "ClientProtos/capture_data.pb.h"
#include "FrameTrack.h"
#include "GpuTrack.h"
#include "GraphTrack.h"
#include "PageFaultsTrack.h"
#include "PickingManager.h"
#include "SchedulerTrack.h"
#include "SystemMemoryTrack.h"
#include "ThreadTrack.h"
#include "Timer.h"
#include "Track.h"
#include "VariableTrack.h"
#include "Viewport.h"

class OrbitApp;

namespace orbit_gl {

class TrackContainer;

// TrackManager is in charge of the active Tracks in Timegraph (their creation, searching, erasing
// and sorting).
class TrackManager {
 public:
  explicit TrackManager(TrackContainer* track_container, TimelineInfoInterface* timeline_info,
                        Viewport* viewport, TimeGraphLayout* layout, OrbitApp* app,
                        const orbit_client_data::ModuleManager* module_manager,
                        orbit_client_data::CaptureData* capture_data);

  [[nodiscard]] std::vector<Track*> GetAllTracks() const;
  [[nodiscard]] const std::vector<Track*>& GetVisibleTracks() const { return visible_tracks_; }
  [[nodiscard]] std::vector<FrameTrack*> GetFrameTracks() const;

  void RequestTrackSorting() { sorting_invalidated_ = true; };
  void SetFilter(const std::string& filter);

  void UpdateTrackListForRendering();

  [[nodiscard]] std::pair<uint64_t, uint64_t> GetTracksMinMaxTimestamps() const;

  [[nodiscard]] static bool IteratableType(orbit_client_protos::TimerInfo_Type type);
  [[nodiscard]] static bool FunctionIteratableType(orbit_client_protos::TimerInfo_Type type);

  Track* GetOrCreateTrackFromTimerInfo(const orbit_client_protos::TimerInfo& timer_info);
  SchedulerTrack* GetOrCreateSchedulerTrack();
  ThreadTrack* GetOrCreateThreadTrack(uint32_t tid);
  [[nodiscard]] std::optional<ThreadTrack*> GetThreadTrack(uint32_t tid) const;
  GpuTrack* GetOrCreateGpuTrack(uint64_t timeline_hash);
  VariableTrack* GetOrCreateVariableTrack(const std::string& name);
  AsyncTrack* GetOrCreateAsyncTrack(const std::string& name);
  FrameTrack* GetOrCreateFrameTrack(const orbit_grpc_protos::InstrumentedFunction& function);
  [[nodiscard]] SystemMemoryTrack* GetSystemMemoryTrack() const {
    return system_memory_track_.get();
  }
  [[nodiscard]] SystemMemoryTrack* CreateAndGetSystemMemoryTrack();
  [[nodiscard]] CGroupAndProcessMemoryTrack* GetCGroupAndProcessMemoryTrack() const {
    return cgroup_and_process_memory_track_.get();
  }
  [[nodiscard]] CGroupAndProcessMemoryTrack* CreateAndGetCGroupAndProcessMemoryTrack(
      const std::string& cgroup_name);
  PageFaultsTrack* GetPageFaultsTrack() const { return page_faults_track_.get(); }
  PageFaultsTrack* CreateAndGetPageFaultsTrack(const std::string& cgroup_name,
                                               uint64_t memory_sampling_period_ms);

  [[nodiscard]] bool GetIsDataFromSavedCapture() const { return data_from_saved_capture_; }
  void SetIsDataFromSavedCapture(bool value) { data_from_saved_capture_ = value; }

  void RemoveFrameTrack(uint64_t function_address);

  void SetTrackTypeVisibility(Track::Type type, bool value);
  [[nodiscard]] bool GetTrackTypeVisibility(Track::Type type) const;

  const absl::flat_hash_map<Track::Type, bool> GetAllTrackTypesVisibility() const;
  void RestoreAllTrackTypesVisibility(const absl::flat_hash_map<Track::Type, bool>& values);

 private:
  [[nodiscard]] int FindMovingTrackIndex();
  void UpdateMovingTrackPositionInVisibleTracks();
  void SortTracks();
  [[nodiscard]] std::vector<ThreadTrack*> GetSortedThreadTracks();
  // Filter tracks that are already sorted in sorted_tracks_.
  void UpdateVisibleTrackList();
  void DeletePendingTracks();

  void AddTrack(const std::shared_ptr<Track>& track);
  void AddFrameTrack(const std::shared_ptr<FrameTrack>& frame_track);

  // TODO(b/174655559): Use absl's mutex here.
  mutable std::recursive_mutex mutex_;

  std::vector<std::shared_ptr<Track>> all_tracks_;
  absl::flat_hash_map<uint32_t, std::shared_ptr<ThreadTrack>> thread_tracks_;
  std::map<std::string, std::shared_ptr<AsyncTrack>> async_tracks_;
  std::map<std::string, std::shared_ptr<VariableTrack>> variable_tracks_;
  // Mapping from timeline to GPU tracks. Timeline name is used for stable ordering. In particular
  // we want the marker tracks next to their queue track. E.g. "gfx" and "gfx_markers" should appear
  // next to each other.
  std::map<std::string, std::shared_ptr<GpuTrack>> gpu_tracks_;
  // Mapping from function id to frame tracks.
  std::map<uint64_t, std::shared_ptr<FrameTrack>> frame_tracks_;
  std::shared_ptr<SchedulerTrack> scheduler_track_;
  std::shared_ptr<SystemMemoryTrack> system_memory_track_;
  std::shared_ptr<CGroupAndProcessMemoryTrack> cgroup_and_process_memory_track_;
  std::shared_ptr<PageFaultsTrack> page_faults_track_;

  // This intermediatly stores tracks that have been deleted from one of the track vectors above
  // so they can safely be removed from the list of sorted and visible tracks.
  // This makes sure tracks are always removed during UpdateLayout(), so no elements retain
  // stale pointers returned in GetAllChildren() or GetNonHiddenChildren().
  std::vector<std::shared_ptr<Track>> deleted_tracks_;

  Viewport* viewport_;
  TimeGraphLayout* layout_;

  std::vector<Track*> sorted_tracks_;
  bool sorting_invalidated_ = false;
  bool visible_track_list_needs_update_ = false;
  Timer last_thread_reorder_;

  std::string filter_;
  std::vector<Track*> visible_tracks_;

  const orbit_client_data::ModuleManager* module_manager_;
  orbit_client_data::CaptureData* capture_data_ = nullptr;

  OrbitApp* app_ = nullptr;
  TrackContainer* track_container_ = nullptr;
  TimelineInfoInterface* timeline_info_ = nullptr;

  bool data_from_saved_capture_ = false;
  absl::flat_hash_map<Track::Type, bool> track_type_visibility_;
};

}  // namespace orbit_gl

#endif  // ORBIT_GL_TRACK_MANAGER_H_
