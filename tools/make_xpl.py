#!/usr/bin/env python3
"""Make MPC .xpl preset files, one folder per bank, from the factory sheets and every program in the banks given (.syx, .h2p, .zip).

The MPC's preset browser shows <documents>/Plugin Presets/Instruments/<plugin folder>/ and its sub-folders, so each bank becomes a
"layer": Factory 1978, then one folder per bank file. Each preset holds the plugin's own state chunk (tools/xpl_chunks.cpp runs the
native plugin to make it), wrapped as the MPC saves it: a VST2 bank chunk ("CcnK" ... "FBCh", the plugin's fxID, version and program
count, 128 reserved bytes, the chunk) in the MPC's text encoding ("<length>.<6-bit characters>", alphabet .A-Za-z0-9+, LSB first).

  python3 tools/make_xpl.py --out OUT [--template SAVED.xpl] [--beta] [--device live3|force] BANK...

--template: a preset saved from this plugin on the device. Its <PLUGIN> line and its bank header (fxID, version, program count) are
reused exactly; without one they are built from the plugin's own values and the device's install path (no fileTime / infoUpdateTime).
Bank files are the user's: none ships with the plugin, and the output is for the user's own device."""
import argparse, datetime, os, re, struct, subprocess, sys

ALPHA = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')


def mpc_encode(b):
    """The MPC's state text (as JUCE's MemoryBlock::toBase64Encoding): length, '.', then 6 bits a character, LSB first."""
    n = len(b)
    out = []
    for i in range((n * 8 + 5) // 6):
        v = 0
        for k in range(6):
            bit = i * 6 + k
            if bit < n * 8 and (b[bit >> 3] >> (bit & 7)) & 1:
                v |= 1 << k
        out.append(ALPHA[v])
    return "%d.%s" % (n, "".join(out))


def mpc_decode(s):
    n, data = s.split('.', 1)
    n = int(n)
    out = bytearray(n)
    bit = 0
    for ch in data:
        v = ALPHA.index(ch)
        for k in range(6):
            if bit < n * 8 and (v >> k) & 1:
                out[bit >> 3] |= 1 << (bit & 7)
            bit += 1
    return bytes(out)


def build_helper(beta):
    sdk = os.environ.get('MPC_BUILDER', os.path.expanduser('~/mpc-plugin-builder-sdk'))
    out = os.path.join(ROOT, 'build', 'xpl')
    os.makedirs(out, exist_ok=True)
    exe = os.path.join(out, 'xpl_chunks_beta' if beta else 'xpl_chunks')
    defs = ['-DP5_NAME="Profit-5 2 Beta"', "-DP5_UID='P','f','5','B'"] if beta else []
    src = lambda p: os.path.join(ROOT, p)
    subprocess.run(['gcc', '-O1', '-std=gnu11', '-c', '-I' + src('src'), '-I' + src('analog'), src('src/p5_import.c'), '-o', os.path.join(out, 'p5_import.o')], check=True)
    subprocess.run(['gcc', '-O1', '-std=gnu11', '-c', '-I' + src('src'), '-I' + src('analog'), src('src/engine.c'), '-o', os.path.join(out, 'engine.o')], check=True)
    subprocess.run(['g++', '-O1', '-std=c++17', '-pthread', '-DP5_TEST', *defs, '-I' + os.path.join(sdk, 'framework'), '-I' + src('src'), '-I' + src('analog'),
                    '-o', exe, src('src/Profit5Plugin.cpp'), src('src/engine_unit.cpp'), src('tools/xpl_chunks.cpp'),
                    os.path.join(out, 'p5_import.o'), os.path.join(out, 'engine.o'), '-lm'], check=True, stderr=subprocess.DEVNULL)
    return exe


def safe(name):
    name = re.sub(r'[\\/:*?"<>|\x00-\x1f]', '_', name).strip().rstrip('.')
    return name or 'Untitled'


def xml_escape(s):
    return s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;').replace('"', '&quot;')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', required=True, help='output folder: it receives "<plugin folder>/<bank>/<NNN name>.xpl"')
    ap.add_argument('--template', help='a preset saved from this plugin on the device (recommended)')
    ap.add_argument('--beta', action='store_true', help='for "Profit-5 2 Beta" (uid Pf5B) instead of the release')
    ap.add_argument('--device', choices=('live3', 'force'), default='live3', help='install path for a preset built without a template')
    ap.add_argument('banks', nargs='*', help='.syx, .h2p or .zip bank files')
    a = ap.parse_args()

    exe = build_helper(a.beta)
    env = dict(os.environ, MPC_PLUGIN_FOLDER=os.path.join(ROOT, 'build', 'xpl'))
    res = subprocess.run([exe, *a.banks], capture_output=True, text=True, env=env, check=True)
    info = re.search(r'PLUGIN fxid (\S{4}) version (\d+) programs (\d+) product "([^"]*)"', res.stderr)
    fxid, fxver, nprog, product = info.group(1).encode(), int(info.group(2)), int(info.group(3)), info.group(4)
    future = bytes(128)

    folder = 'sd88me - VST - ' + product
    if a.template:
        t = open(a.template, encoding='utf-8', errors='replace').read()
        plugin_xml = re.search(r'<PLUGIN\b.*?/>', t, re.S).group(0)
        raw = mpc_decode(re.search(r'<state>([^<]*)</state>', t).group(1))
        magic, _, kind, _, t_fxid, t_fxver, t_nprog = struct.unpack('>4sI4sI4sII', raw[:28])
        if magic != b'CcnK' or kind != b'FBCh':
            sys.exit('the template is not a saved plugin preset (no CcnK/FBCh bank chunk)')
        if t_fxid != fxid:
            sys.exit('the template is for plugin %r, not %r: save one from %s' % (t_fxid.decode(), fxid.decode(), product))
        fxver, nprog, future = t_fxver, t_nprog, raw[28:156]
    else:
        uid_hex = fxid.hex()
        if a.device == 'live3':
            path = '/nvme/VST2 Plugins/%s/VST/%s' % (folder, 'profit52beta-arm64.so' if a.beta else 'profit5-arm64.so')
        else:
            path = '/sdcard/Synths/%s/%s' % (folder, 'VST/profit52beta-arm32.so' if a.beta else 'profit_5.so')
        plugin_xml = ('<PLUGIN name="%s" format="VST" category="Synth" manufacturer="sd88me"\r\n'
                      '          version="2.0.0.0" file="%s"\r\n'
                      '          uid="%s" isInstrument="1" numInputs="0" numOutputs="2" isShell="0"/>') % (xml_escape(product), xml_escape(path), uid_hex)

    date = datetime.datetime.now().strftime('%Y-%m-%d-%H-%M')
    counts = {}
    for line in res.stdout.splitlines():
        bank, name, hexchunk = line.split('\t')
        ch = bytes.fromhex(hexchunk)
        body = b'FBCh' + struct.pack('>I', 1) + fxid + struct.pack('>II', fxver, nprog) + future + struct.pack('>I', len(ch)) + ch
        state = b'CcnK' + struct.pack('>I', 0) + body                  # the MPC writes 0 for the byte size
        bank_dir = os.path.join(a.out, folder, safe(bank.replace('_', ' ')))
        os.makedirs(bank_dir, exist_ok=True)
        preset = re.sub(r'^\d{3} ', '', name)
        xml = ('<?xml version="1.0" encoding="UTF-8"?>\r\n\r\n<pluginstate>\r\n'
               '  <version file="1" date="%s"/>\r\n  %s\r\n  <preset>%s</preset>\r\n  <state>%s</state>\r\n</pluginstate>\r\n'
               % (date, plugin_xml, xml_escape(preset), mpc_encode(state)))
        with open(os.path.join(bank_dir, safe(name) + '.xpl'), 'w', encoding='utf-8', newline='') as f:
            f.write(xml)
        counts[bank] = counts.get(bank, 0) + 1
    for b, n in counts.items():
        print('%-40s %4d presets' % (b, n))
    print('written to', os.path.join(a.out, folder))


if __name__ == '__main__':
    main()
