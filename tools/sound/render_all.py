"""All AdLib renders -> work/sound/*.wav (needs work/oplrender.exe; build line in tools/sound/README in S2.4 notes).
  python tools/sound/render_all.py        ASOUND.117 commands 0-45 + ASOUND.LOG + SPEECH.117"""
import os, struct, subprocess, sys
sys.path.insert(0, os.path.dirname(__file__))
import drvemu

os.makedirs('work/sound', exist_ok=True)


def wav(name, log):
    p = 'work/sound/%s.opl' % name
    with open(p, 'wb') as f:
        for t, r, v in log:
            f.write(struct.pack('<IHB', t, r, v))
    subprocess.check_call([os.path.abspath('work/oplrender.exe'), p, 'work/sound/%s.wav' % name])
    os.remove(p)


# SPEECH.117: 8-bit unsigned PCM, 8000 Hz (ASOUND cs:09AA clip table, RE_GUIDE SS2.4); clip starts
SPEECH_CLIPS = [0, 4216, 9251, 10572, 12829, 15654, 18343, 22119, 26408]


def pcm_wav(path, data, rate=8000):
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVEfmt ' +
                struct.pack('<IHHIIHH', 16, 1, 1, rate, rate, 1, 8) + b'data' + struct.pack('<I', len(data)) + data)


def speech():
    d = open('Game/SPEECH.117', 'rb').read()
    pcm_wav('work/sound/speech_all.wav', d)
    for i in range(8):
        pcm_wav('work/sound/speech_%d.wav' % i, d[SPEECH_CLIPS[i]:SPEECH_CLIPS[i + 1]])


def main():
    D = drvemu.render_log('Game/ASOUND.LOG', 2, 8)      # logo jingle (MPS_LOGO, sound-driver letter A)
    wav('asound_log_logo', D.log)
    print('asound_log_logo  %d writes' % len(D.log))
    speech()
    for c in range(46):
        D = drvemu.render(c, 60, 300)
        if D.log:
            wav('asound_cmd%02d' % c, D.log)
            print('asound_cmd%02d  %5d writes  %.1f s' % (c, len(D.log), D.log[-1][0] / drvemu.OPL_RATE))


if __name__ == '__main__':
    main()
