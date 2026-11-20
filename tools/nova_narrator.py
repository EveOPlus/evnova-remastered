"""Local Piper mission narrator. Latest stdin request replaces all prior speech."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import threading
import time
import wave
import winsound


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--model', type=Path, required=True)
    parser.add_argument('--state', type=Path, required=True)
    parser.add_argument('--silent', action='store_true')
    args = parser.parse_args()
    args.state.parent.mkdir(parents=True, exist_ok=True)
    condition = threading.Condition()
    latest = [0, args.model.stem, '']
    closed = threading.Event()

    def publish(request_id, status, **fields):
        # Serialize status with request replacement so stale synthesis cannot
        # overwrite the state of a newer selection or stop command.
        with condition:
            if request_id != latest[0]:
                return
            temporary = args.state.with_suffix('.tmp')
            temporary.write_text(json.dumps(dict(id=request_id, status=status, voice=latest[1], **fields)), encoding='utf-8')
            for _ in range(8):
                try:
                    os.replace(temporary, args.state)
                    break
                except PermissionError:
                    # Windows readers can briefly hold a file without delete
                    # sharing. Telemetry must never interrupt speech.
                    time.sleep(.015)

    def read_requests():
        import sys
        try:
            for line in sys.stdin:
                fields = line.rstrip('\n').split('\t', 2)
                if len(fields) == 2:
                    # Older installed game builds send only request + text.
                    number, encoded = fields
                    voice_id = args.model.stem
                elif len(fields) == 3:
                    number, voice_id, encoded = fields
                else:
                    continue
                if not re.fullmatch(r'[A-Za-z0-9_-]+', voice_id):
                    continue
                text = bytes.fromhex(encoded).decode('utf-8')
                with condition:
                    latest[:] = [int(number), voice_id, text]
                    if not args.silent:
                        winsound.PlaySound(None, winsound.SND_PURGE)
                    publish(latest[0], 'queued' if text else 'stopped', text=text)
                    condition.notify_all()
        finally:
            closed.set()
            if not args.silent:
                winsound.PlaySound(None, winsound.SND_PURGE)
            with condition:
                condition.notify_all()

    # Load native inference libraries before starting a thread blocked on stdin;
    # Windows DLL initialization can otherwise deadlock with that pipe reader.
    from piper import PiperVoice, SynthesisConfig
    voice = PiperVoice.load(str(args.model))
    loaded_voice = args.model.stem
    threading.Thread(target=read_requests, daemon=True).start()
    last_id = -1
    while not closed.is_set():
        with condition:
            condition.wait_for(lambda: closed.is_set() or latest[0] != last_id)
            request_id, voice_id, text = latest
            last_id = request_id
        if closed.is_set():
            break
        if not text:
            continue
        try:
            if loaded_voice != voice_id:
                publish(request_id, 'loading', text=text)
                voice = PiperVoice.load(str(args.model.parent / f'{voice_id}.onnx'))
                loaded_voice = voice_id
            if closed.is_set() or latest[0] != request_id:
                continue
            publish(request_id, 'generating', text=text)
            # Piper yields one sentence at a time, allowing prompt cancellation
            # and playback before a long mission has finished synthesizing.
            for index, chunk in enumerate(voice.synthesize(text, SynthesisConfig(length_scale=1.0))):
                if closed.is_set() or latest[0] != request_id:
                    break
                output = args.state.parent / f'mission-voice-{index % 2}.wav'
                with wave.open(str(output), 'wb') as wav:
                    wav.setnchannels(chunk.sample_channels)
                    wav.setsampwidth(chunk.sample_width)
                    wav.setframerate(chunk.sample_rate)
                    wav.writeframes(chunk.audio_int16_bytes)
                with condition:
                    if closed.is_set() or latest[0] != request_id:
                        break
                    if not args.silent:
                        winsound.PlaySound(str(output), winsound.SND_FILENAME | winsound.SND_ASYNC)
                    duration = len(chunk.audio_int16_bytes) / (chunk.sample_rate * chunk.sample_width * chunk.sample_channels)
                    publish(request_id, 'speaking', text=text, sentence=index, audio_file=str(output), seconds=duration)
                    interrupted = condition.wait_for(lambda: closed.is_set() or latest[0] != request_id, timeout=duration)
                if interrupted:
                    break
            else:
                publish(request_id, 'finished', text=text)
        except Exception as error:
            publish(request_id, 'error', message=str(error), text=text)


if __name__ == '__main__':
    main()
