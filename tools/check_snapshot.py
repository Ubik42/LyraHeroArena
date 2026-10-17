"""Check the published source snapshot, local documentation links and recorded aggregates.

This is an offline content check, not an Unreal build or gameplay test.
Run: python tools/check_snapshot.py
"""
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
from urllib.parse import unquote

ROOT=Path(__file__).resolve().parents[1]
errors=[]
manifest=json.loads((ROOT/'docs/data/source-manifest.json').read_text(encoding='utf-8'))
for name,expected in manifest['files'].items():
    path=ROOT/name
    if not path.is_file():
        errors.append('Missing source: '+name)
        continue
    actual=hashlib.sha256(path.read_text(encoding='utf-8-sig').replace('\r\n','\n').encode()).hexdigest()
    if actual!=expected:
        errors.append('Source hash differs: '+name)

for path in ROOT.rglob('*.md'):
    if '.git' in path.parts:
        continue
    text=path.read_text(encoding='utf-8-sig')
    links=re.findall(r'!?\[[^\]]*\]\(([^)]+)\)',text)+re.findall(r'<img[^>]+src="([^"]+)"',text)
    for link in links:
        if re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:|^#',link):
            continue
        target=unquote(link.split('#')[0])
        if target and not (path.parent/target).is_file():
            errors.append(f'Broken local link: {path.relative_to(ROOT)} -> {link}')

performance=json.loads((ROOT/'docs/data/performance-results.json').read_text(encoding='utf-8'))
for key,case in performance['cases'].items():
    for metric in case['metrics']:
        for variant,field in [('Baseline','baseline'),('Optimized','optimized')]:
            values=[run['metrics'][metric['metric']][metric['statistic']] for run in case['runs'] if run['variant']==variant]
            if len(values)!=3 or not math.isclose(statistics.median(values),metric[field],rel_tol=1e-9):
                errors.append(f'Aggregate mismatch: {key}/{metric["metric"]}/{variant}')

network=json.loads((ROOT/'docs/data/gas-network.json').read_text(encoding='utf-8'))
for net in ('2','3'):
    before,switched,respawn=[network['stages'][stage][net] for stage in ('before','switch','respawn')]
    if not (before['hero']=='Striker' and before['count']==7 and switched['hero']==respawn['hero']=='Phase'
            and switched['count']==respawn['count']==4 and switched['specs']==respawn['specs']
            and all(spec.startswith('Phase:') for spec in respawn['specs'])
            and before['pawn']!=respawn['pawn'] and respawn['pawn']==respawn['avatar']):
        errors.append('GAS lifecycle record mismatch: '+net)

blink=json.loads((ROOT/'docs/data/blink-network.json').read_text(encoding='utf-8'))
for case in ('after','weak'):
    owner=blink['owner_player_ids'][case]
    for stage,expected in (('later',3),('accepted',2),('burst',0),('recharge',1)):
        pair=[blink['cases'][case][stage][net][owner] for net in ('2','3')]
        if any(row['charges']!=expected for row in pair) or sum((a-b)**2 for a,b in zip(pair[0]['position'],pair[1]['position']))>=4:
            errors.append('Blink reconciliation record mismatch: '+case+'/'+stage)
        if pair[1]['pending'] not in (None,0):
            errors.append('Blink unresolved prediction: '+case+'/'+stage)
owner=blink['owner_player_ids']['weak']
early=blink['cases']['weak']['early']['3'][owner]
reset=blink['cases']['weak']['hero-reset']['3'][owner]
if (early['charges'],early['pending'],reset['charges'],reset['pending'])!=(2,1,3,0):
    errors.append('Blink prediction or reset path was not exercised')

# Separate executable processes: validate recorded transitions, not the stored pass flag.
for case in ('separate-network', 'separate-network-baseline', 'countdown-lifecycle'):
    record=json.loads((ROOT/f'docs/data/{case}.json').read_text(encoding='utf-8'))
    if any(value!=0 for value in record['exit_codes'].values()):
        errors.append(case+': process exit')
    for role,stages in record['roles'].items():
        for stage,expected in [('before',3),('rejected',3),('accepted',2),('recharged',3),('phase-reset',3),('respawn',3)]:
            if stages[stage]['charges']!=expected or stages[stage]['pending']!=0:
                errors.append(case+'/'+role+'/'+stage+': charge state')
        for stage,hero,count in [('striker','Striker',7),('phase-reset','Phase',4),('respawn','Phase',4)]:
            gas=stages[stage]['gas']
            if gas['hero']!=hero or gas['count']!=count or len(set(gas['specs']))!=count or gas['pawn']!=gas['avatar']:
                errors.append(case+'/'+role+'/'+stage+': GAS state')
        if stages['phase-reset']['pawn']==stages['respawn']['pawn'] or stages['accepted']['pawn']!=stages['recharged']['pawn']:
            errors.append(case+'/'+role+': lifecycle identity')
        if math.dist(stages['before']['position'],stages['rejected']['position'])>2 or math.dist(stages['before']['position'],stages['accepted']['position'])<100:
            errors.append(case+'/'+role+': movement')
    for stage in ('before','rejected','accepted','recharged','phase-reset','respawn'):
        a,b=[record['roles'][role][stage] for role in ('server','client')]
        if a['player']!=b['player'] or math.dist(a['position'],b['position'])>2:
            errors.append(case+'/'+stage+': replicated player state')
    if record['client_prediction_rejections']<1:
        errors.append(case+': rejection path not exercised')
    if case=='separate-network':
        early=record['roles']['client']['early']
        if (early['charges'],early['pending'])!=(2,1):errors.append(case+': no early prediction')

match=json.loads((ROOT/'docs/data/full-match.json').read_text(encoding='utf-8'))
post=[row for row in match['samples'] if row['postGame']]
if not post or match['score'][match['winner']-1]!=match['targetScore'] or match['postGameHoldSeconds']<5:
    errors.append('Complete match did not reach its recorded winning condition')
if any(row['playing'] or row['warmup'] or [row['team1'],row['team2']]!=match['score'] for row in post):
    errors.append('Post-game phase or frozen score mismatch')
if any(b['time']<=a['time'] or b['team1']<a['team1'] or b['team2']<a['team2'] for a,b in zip(match['samples'],match['samples'][1:])):
    errors.append('Match samples are not a continuous forward progression')

# Validate the new countdown records independently of their stored pass flags.
countdown=json.loads((ROOT/'docs/data/blink-countdown.json').read_text(encoding='utf-8'))
if len(countdown['runs']) < 2:
    errors.append('Missing countdown conditions')
for run in countdown['runs']:
    if any(code != 0 for code in run['exit_codes'].values()): errors.append('Countdown process exit')
    for role,rows in run['roles'].items():
        if rows[0]['charges'] != 3 or rows[-1]['charges'] != 3 or len({r['pawn'] for r in rows}) != 1:
            errors.append('Countdown endpoint/Pawn mismatch')
        if any(not 0 <= r['remaining_seconds'] <= 4.02 for r in rows): errors.append('Countdown exceeds cycle')
        for a,b in zip(rows,rows[1:]):
            if a['charges'] == b['charges'] < 3 and not a['pending'] and not b['pending'] and b['remaining_seconds'] > a['remaining_seconds'] + .02:
                errors.append('Countdown rewinds within the same charge count')
        if run['burst'] and not any(r['charges'] == 0 and r['pending'] == 0 for r in rows):
            errors.append('Burst countdown did not consume all charges')
    if run['injected_clock_bias_seconds'] and not any(s['legacy_formula_seconds'] > 4 and s['display_seconds'] <= 4.02 for s in run['snapshots']):
        errors.append('Missing paired legacy/new clock comparison')

# Public material excludes project payloads, credentials and workstation-specific paths.
for folder in ('Plugins','docs','assets'):
    for path in (ROOT/folder).rglob('*'):
        if not path.is_file():
            continue
        if path.suffix.lower() in {'.uasset','.umap','.dll','.exe','.pdb','.pem','.key'}:
            errors.append('Unexpected payload: '+str(path.relative_to(ROOT)))
        if path.suffix.lower() in {'.md','.json','.ini','.cpp','.h','.cs','.uplugin'}:
            text=path.read_text(encoding='utf-8-sig')
            if re.search(r'\b[A-Za-z]:[\\/]|-----BEGIN .*PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{20,}|AIza[A-Za-z0-9_-]{30,}',text):
                errors.append('Review local path or credential pattern: '+str(path.relative_to(ROOT)))

if errors:
    raise SystemExit('\n'.join(errors))
print(f'PASS: {len(manifest["files"])} source hashes; local Markdown/image links; 12-run aggregates; GAS, Blink and full-match records; payload/path scan.')
print('Scope: checks saved content only, not runtime, build compatibility or legal clearance.')
