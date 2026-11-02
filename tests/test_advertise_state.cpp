#include "../ledup_advertise_state.h"

#include <cassert>

int main() {
  ledup::AdvertiseState state;

  assert(state.phase() == ledup::AdvertisePhase::IDLE);
  assert(state.begin_config(10));
  assert(state.phase() == ledup::AdvertisePhase::CONFIGURING);
  assert(!state.phase_timed_out(509, 500));
  assert(state.phase_timed_out(510, 500));
  assert(state.config_complete(true, 20));
  assert(state.phase() == ledup::AdvertisePhase::STARTING);
  assert(state.start_complete(true, 100));
  assert(state.phase() == ledup::AdvertisePhase::ADVERTISING);
  assert(!state.should_stop(599, 500));
  assert(state.should_stop(600, 500));
  assert(state.begin_stop(600));
  assert(state.phase() == ledup::AdvertisePhase::STOPPING);
  assert(state.stop_complete(true, 610));
  assert(state.phase() == ledup::AdvertisePhase::IDLE);

  assert(state.begin_config(700));
  assert(!state.config_complete(false, 710));
  assert(state.phase() == ledup::AdvertisePhase::IDLE);

  assert(state.begin_config(800));
  assert(state.config_complete(true, 810));
  assert(!state.start_complete(false, 820));
  assert(state.phase() == ledup::AdvertisePhase::IDLE);

  assert(state.begin_config(900));
  assert(state.config_complete(true, 910));
  assert(state.start_complete(true, 920));
  assert(state.begin_stop(1420));
  assert(!state.stop_complete(false, 1430));
  assert(state.phase() == ledup::AdvertisePhase::ADVERTISING);
  assert(state.begin_stop(1440));
  assert(state.stop_request_failed(1450));
  assert(state.phase() == ledup::AdvertisePhase::ADVERTISING);

  ledup::CommandQueue<4> queue;
  assert(queue.push({7, 22}));
  assert(queue.push({7, 19}));
  assert(queue.push({7, 20}));
  assert(queue.push({7, 10}));
  assert(!queue.push({7, 16}));
  assert(queue.size() == 4);

  ledup::Command command{};
  assert(queue.pop(command));
  assert(command.remote_group == 7 && command.key_code == 22);
  assert(queue.pop(command));
  assert(command.remote_group == 7 && command.key_code == 19);
  assert(queue.pop(command));
  assert(command.remote_group == 7 && command.key_code == 20);
  assert(queue.pop(command));
  assert(command.remote_group == 7 && command.key_code == 10);
  assert(!queue.pop(command));

  return 0;
}
