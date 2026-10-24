import unittest
from unittest.mock import patch
from activity import ActivityLog, MAX_EVENTS
import server


def packet(seq=1, ms=10000, start=9000, duration=1000, **extra):
    values = dict(kind='packet', seq=seq, ms=ms, startMs=start, durationMs=duration,
                  direction='rx', type='data', flags=0, profile='P0')
    values.update(extra)
    return 'GROUND_EVENT ' + ' '.join(f'{k}={v}' for k, v in values.items())


class ActivityTests(unittest.TestCase):
    def test_receive_airtime_and_direction(self):
        log=ActivityLog();self.assertTrue(log.feed(packet(),now=100))
        e=log.snapshot(100)['events'][0]
        self.assertEqual(e['start'],99);self.assertEqual(e['duration'],1)
        self.assertEqual(e['direction'],'rx')

    def test_long_packet_is_retained_while_any_part_is_visible(self):
        log=ActivityLog();log.feed(packet(ms=120000,start=0,duration=120000),now=120)
        self.assertEqual(len(log.snapshot(175)['events']),1)
        self.assertEqual(log.snapshot(181)['events'],[])

    def test_silence_clock_moves_without_new_events(self):
        log=ActivityLog();a=log.snapshot(100);b=log.snapshot(120)
        self.assertEqual(a['events'],b['events']);self.assertEqual(b['server_now']-a['server_now'],20)

    def test_sequence_gap_duplicate_and_wrap(self):
        log=ActivityLog();log.feed(packet(seq=0xffffffff,ms=0xfffffff0,start=0xffffff00,duration=240),now=100)
        self.assertTrue(log.feed(packet(seq=1,ms=10,start=0xfffffff0,duration=26),now=100.026))
        self.assertEqual(log.dropped,1)
        self.assertFalse(log.feed(packet(seq=1,ms=10,start=0xfffffff0,duration=26),now=100.026))

    def test_reboot_starts_new_generation(self):
        log=ActivityLog();log.feed(packet(seq=20,ms=200000,start=199000),now=200)
        generation=log.generation
        log.feed(packet(seq=1,ms=1000,start=0),now=201)
        self.assertNotEqual(log.generation,generation);self.assertEqual(len(log.events),1)

    def test_invalid_events_do_not_create_packets(self):
        for line in [packet(type='bogus'),packet(profile='P8'),packet(duration=-1),
                     packet(start=10000),packet(direction='maybe'),packet(seq=-1),
                     'GROUND_EVENT kind=packet seq=broken',packet(kind='other')]:
            with self.subTest(line=line):self.assertFalse(ActivityLog().feed(line,now=100))

    def test_boundaries_and_bounded_history(self):
        log=ActivityLog()
        self.assertTrue(log.feed(packet(kind='window',phase='control',durationMs=10000,startMs=10000),now=100))
        self.assertEqual(log.snapshot(100)['window']['phase'],'control')
        for seq in range(2,MAX_EVENTS+50):log.feed(packet(seq=seq),now=100)
        self.assertEqual(len(log.events),MAX_EVENTS)

    def test_window_heartbeat_is_not_another_boundary(self):
        log=ActivityLog()
        log.feed(packet(kind='window',phase='data',startMs=10000,durationMs=30000),now=100)
        log.feed(packet(seq=2,kind='window',phase='data',ms=12000,startMs=10000,durationMs=30000),now=102)
        self.assertEqual(len(log.events),1)
        self.assertEqual(log.window['start'],100)
        self.assertEqual(log.dropped,0)

    def test_beacon_countdown_refinements_do_not_split_acquisition(self):
        log=ActivityLog()
        log.feed(packet(kind='window',phase='acquiring',ms=10000,startMs=10000,durationMs=40000),now=100)
        log.feed(packet(seq=2,kind='window',phase='acquiring',ms=12000,startMs=12000,durationMs=38000),now=102)
        self.assertEqual(len(log.events),1)
        self.assertEqual(log.window['start'],100)
        self.assertEqual(log.window['duration'],40)

    def test_gap_warning_ages_out_with_its_history(self):
        log=ActivityLog();log.feed(packet(seq=1),now=100);log.feed(packet(seq=3),now=101)
        self.assertEqual(log.snapshot(101)['dropped_events'],1)
        self.assertEqual(log.snapshot(162)['dropped_events'],0)
        self.assertEqual(log.snapshot(162)['total_dropped_events'],1)

    def test_activity_does_not_refresh_balloon_telemetry(self):
        before=dict(server.state);old=server.activity
        try:
            server.activity=ActivityLog();server.state.update(source='ground',received_at=10,environment_received_at=10)
            with patch('activity.time.time',return_value=20):server.consume_line(packet())
            self.assertEqual(server.state['received_at'],10)
            self.assertEqual(server.state['environment_received_at'],10)
            self.assertEqual(len(server.activity.events),1)
        finally:
            server.state.clear();server.state.update(before);server.activity=old


if __name__=='__main__':unittest.main()
