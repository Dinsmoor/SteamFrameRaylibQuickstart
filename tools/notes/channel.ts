#!/usr/bin/env bun
// channel.ts - a Claude Code channel that pushes design notes from the headset
// into the live session (docs/NOTES.md; the contract: code.claude.com/docs/en/channels-reference).
//
// Claude Code spawns this (it's the "notes" server in .mcp.json) and talks MCP
// over stdio. It tails local-data/notes/*.jsonl, where notesd.py appends one
// line per transcribed note, and sends each new line as a channel event:
//
//   <channel source="notes" app="toolbox" id="note-..." place="garden (...)" ...>
//   "make a variant of this bug that flies"   (pointing at garden.bug 3)
//   </channel>
//
// One-way: Claude acts on the note in the session; there's nothing to reply
// to in the headset yet. Only lines appended after it starts are sent (what
// was said before this session is in the file for /notes to read).
//
// Channels are a research preview, so the session starts with
//   claude --dangerously-load-development-channels server:notes
// (scripts/notes.sh claude does that).
import { Server } from '@modelcontextprotocol/sdk/server/index.js'
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js'
import { watch, readdirSync, statSync, openSync, readSync, closeSync, existsSync, mkdirSync } from 'node:fs'
import { join, basename } from 'node:path'

const ROOT = join(import.meta.dir, '..', '..')
const DIR = join(ROOT, 'local-data', 'notes')

const mcp = new Server(
  { name: 'notes', version: '0.1.0' },
  {
    capabilities: { experimental: { 'claude/channel': {} } },
    instructions:
      'Events from the notes channel are design notes the developer said inside the VR headset ' +
      '(<channel source="notes" app=... id=... place=... pointing_left=... pointing_right=... looking=...>). ' +
      'The body is the transcript, then the context the app recorded. Treat each as an instruction from the ' +
      'developer about the thing they were pointing at or looking at (registry names like "garden.bug 3" are ' +
      'the app\'s widget/mark names; see docs/NOTES.md). Act on it with the /notes skill: read the note\'s JSON ' +
      'and screenshot, make the change, rebuild, and deploy when it\'s ready. One-way: no reply tool.',
  },
)
await mcp.connect(new StdioServerTransport())

// each .jsonl: how far we've read
const offsets = new Map<string, number>()
mkdirSync(DIR, { recursive: true })
for (const f of readdirSync(DIR)) if (f.endsWith('.jsonl')) offsets.set(join(DIR, f), statSync(join(DIR, f)).size)

function readNew(path: string) {
  const size = statSync(path).size
  const from = offsets.get(path) ?? 0
  if (size <= from) { if (size < from) offsets.set(path, 0); return }
  const buf = Buffer.alloc(size - from)
  const fd = openSync(path, 'r')
  readSync(fd, buf, 0, buf.length, from)
  closeSync(fd)
  offsets.set(path, size)
  for (const line of buf.toString('utf8').split('\n')) {
    if (!line.trim()) continue
    let n: any
    try { n = JSON.parse(line) } catch { continue }
    send(n)
  }
}

function s(v: any): string { return v == null ? '' : String(v) }

async function send(n: any) {
  const ctx = n.context ?? {}
  const meta: Record<string, string> = {
    app: s(n.app), id: s(n.id), seconds: s(n.seconds), clock: s(n.clock),
    place: s(ctx.place), pointing_left: s(ctx.pointing?.left), pointing_right: s(ctx.pointing?.right),
    looking: s(ctx.looking), looking_by: s(ctx.looking_by),
    json: join(s(n.dir), s(n.id) + '.json'), screenshot: s(n.screenshot),
  }
  for (const k of Object.keys(meta)) if (!meta[k]) delete meta[k]
  const lines = [
    n.text ? `"${n.text}"` : '(nothing was heard)',
    `context: ${JSON.stringify(ctx)}`,
    `head at ${JSON.stringify(n.head?.pos)} looking ${JSON.stringify(n.head?.forward)}; note ${n.id} in ${n.app}` +
      (n.screenshot ? `; screenshot ${n.screenshot}` : ''),
  ]
  await mcp.notification({ method: 'notifications/claude/channel', params: { content: lines.join('\n'), meta } })
}

// inotify on the directory, plus a slow poll in case an event is missed
watch(DIR, (_ev, name) => {
  if (!name || !String(name).endsWith('.jsonl')) return
  const p = join(DIR, String(name))
  if (existsSync(p)) readNew(p)
})
setInterval(() => {
  for (const f of readdirSync(DIR)) if (f.endsWith('.jsonl')) readNew(join(DIR, f))
}, 3000)
console.error(`notes channel: watching ${DIR} (${[...offsets.keys()].map(k => basename(k)).join(', ') || 'no files yet'})`)
