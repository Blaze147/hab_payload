"""Bounded, observer-only RF timeline. All timestamps are estimates in seconds."""
from collections import deque
import time
import uuid

HISTORY_SECONDS = 60
MAX_EVENTS = 512
PHASES = {'data', 'control', 'beacon', 'acquiring', 'searching', 'stopped', 'fault'}


def signed_delta(a, b):
    return ((a - b + 0x80000000) & 0xffffffff) - 0x80000000


class ActivityLog:
    def __init__(self):
        self.generation = uuid.uuid4().hex
        self.events = deque(maxlen=MAX_EVENTS)
        self.window = None
        self.last_sequence = None
        self.last_uptime = None
        self.dropped = 0
        self.gaps = deque(maxlen=MAX_EVENTS)

    def feed(self, line, now=None):
        if not line.startswith('GROUND_EVENT '):
            return False
        now = time.time() if now is None else now
        try:
            fields = dict(token.split('=', 1) for token in line.split()[1:])
            kind = fields['kind']
            seq, ms, start, duration = (int(fields[k]) for k in ('seq', 'ms', 'startMs', 'durationMs'))
            if any(not 0 <= v <= 0xffffffff for v in (seq, ms, start)):
                return False
            delta = signed_delta(start, ms)
            if not -7200000 <= delta <= 1000 or not 0 <= duration <= 7200000:
                return False
            profile = fields['profile']
            if profile not in {f'P{i}' for i in range(5)}:
                return False
            event = {'id': seq, 'kind': kind, 'host_start_ms': start, 'start': now + delta / 1000,
                     'duration': duration / 1000, 'received_at': now, 'profile': profile}
            if kind == 'packet':
                if fields['type'] not in {'data', 'control', 'beacon'} or fields['direction'] not in {'rx', 'tx'}:
                    return False
                flags = int(fields['flags'])
                restarted = int(fields.get('restarted', '0'))
                if restarted not in (0, 1):
                    return False
                if not 0 <= flags <= 255 or duration == 0 or duration > 120000:
                    return False
                if restarted and not (fields['type'] == 'data' and flags & 128
                                      or fields['type'] == 'control' and flags == 128):
                    return False  # Beacons and SYN/SYN-ACK/ACK never carry it.
                # RX is known only on completion; never invent an in-flight RX.
                if fields['direction'] == 'rx' and delta + duration > 100:
                    return False
                event.update(type=fields['type'], direction=fields['direction'], flags=flags, restarted=bool(restarted))
            elif kind == 'window':
                if fields['phase'] not in PHASES:
                    return False
                event['phase'] = fields['phase']
            else:
                return False
        except (KeyError, ValueError):
            return False
        if self.last_uptime is not None and signed_delta(ms, self.last_uptime) < -1000:
            self.__init__()  # Host reboot; retain no misleading old-clock events.
        if self.last_sequence is not None:
            step = signed_delta(seq, self.last_sequence)
            if step <= 0:
                return False
            self.dropped += step - 1
            if step > 1:
                self.gaps.append((now, step - 1))
        self.last_sequence, self.last_uptime = seq, ms
        same_window = kind == 'window' and self.window and event['phase'] == self.window['phase'] and (event['host_start_ms'] == self.window['host_start_ms'] or event['phase'] == 'acquiring')
        if same_window:
            # Heartbeat updates initialize reconnecting browsers without drawing
            # another boundary for the same actual window.
            self.window.update(received_at=now, profile=event['profile'], duration=max(0,event['start']+event['duration']-self.window['start']))
            return True
        self.events.append(event)
        if kind == 'window':
            self.window = event.copy()
        self._prune(now)
        return True

    def _prune(self, now):
        # Packet completion can be older than a subsequently queued window event.
        while self.gaps and self.gaps[0][0] < now - HISTORY_SECONDS:
            self.gaps.popleft()
        retained = [e for e in self.events if e['start'] + e['duration'] >= now - HISTORY_SECONDS]
        self.events.clear()
        self.events.extend(retained)

    def snapshot(self, now=None):
        now = time.time() if now is None else now
        self._prune(now)
        return {'generation': self.generation, 'server_now': now,
                'history_seconds': HISTORY_SECONDS, 'events': list(self.events),
                'window': self.window, 'dropped_events': sum(count for _, count in self.gaps),
                'total_dropped_events': self.dropped,
                'timing': 'estimated_from_host_timestamps_and_lora_airtime'}
