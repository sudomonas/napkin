# Napkin — Revised Specification (v2)

> The single source of truth for Napkin: product philosophy and buildable
> contract in one document. Supersedes and absorbs the original
> `PROJECRT_DETAILS.txt`, which has been removed.

**A persistent scratch surface for your computer.**
Put it here. Use it. Decide later whether it matters.

---

## 0. What changed from v1, and why

| Cut | Reason |
|---|---|
| Clipboard *history / manager* | Klipper and friends already do this. Never was the product. |
| Multi-format file paste (`CF_HDROP`, `x-special/gnome-copied-files`, cut-vs-copy verbs) | Highest platform-risk surface in the whole spec, for a capability almost nobody uses. |
| Drag and drop | Second-highest platform risk. Paste covers ~90% of real capture. Re-addable later; the item model is designed so it drops in cleanly. |
| Generic file attachments (PDF, zip, arbitrary binaries) | Kills the large-file policy, MIME sniffing, "reveal in file manager", broken-link handling, and the "am I a file manager?" identity crisis — all at once. |
| macOS/Windows as v1 targets | Linux-first. Portability preserved in architecture, not paid for in v1 schedule. |

**Kept, deliberately:** paste (`Ctrl+V`) and a narrow image file picker. See §3.

**Net effect:** the two item types are `text` and `image`. That is the whole
content model. Every simplification below follows from it.

---

## 1. Product philosophy

Napkin is inspired by the physical idea of writing something down on a napkin.

```text
Have something
      |
Throw it into Napkin
      |
Use it later
      |
Decide whether it matters
      |
Keep it, or let it go
```

The user should never have to think:

```text
What should I call this?   Which category?   Which folder?
Which notebook?            Which tags?       Which project?
```

Instead: **just put it on the napkin.**

### What Napkin is not

Not Evernote, Notion, Obsidian, or OneNote. Not a task manager, document
manager, knowledge base, project manager, or file manager. Not a clipboard
history application — the OS already has one, and that was never the product.
Not "a simplified Notion" or "a prettier clipboard manager."

### Mess is a feature

The application must tolerate accumulation. A user should be able to have a
shell command, a random thought, a screenshot, a URL, a copied paragraph, and
another screenshot all sitting together, unsorted, indefinitely.

Napkin exists specifically to **postpone the decision** to organize. Forcing
organization at capture time is the failure this product is designed to avoid.
Ten buffers or five thousand, the app should not care.

### No automatic intelligence

Napkin never summarizes, categorizes, names, classifies, tags, interprets
screenshots, or analyzes content. The application is deterministic and local.

Generated previews from obvious local metadata are fine and expected — first
line of text, URL hostname, image dimensions, item count. That is derived
display, not inference. There is no AI in Napkin.

### The editor stays small

Plain text only: multiline input, selection, copy/paste, undo/redo, keyboard
navigation. Deliberately **not** in v1: Markdown preview, rich text, formatting,
tables, code blocks, syntax highlighting, WYSIWYG.

If a user pastes Markdown, Napkin stores Markdown as text. That is the whole
feature.

**One exception: arithmetic on request** (§7 "Calculating a line"). `Ctrl+Tab`
writes the answer to the calculation on the caret's line into the text, as
ordinary characters. It passes §21 as *use it*: a sum on a napkin is there to
be worked out. It is deterministic and local, so it is not the automatic
intelligence ruled out above, and it stays small by construction. Nothing is
evaluated until asked, nothing is rendered that is not in the text, and there
is no new item type, format or mode.

## 2. Invariants

These are testable MUSTs. Everything else in this document is guidance.

1. A **kept** buffer is never deleted by any automatic process.
2. Nothing is ever hard-deleted without passing through the trash first.
3. Napkin never modifies, moves, or deletes a file outside its own data directory.
4. Napkin makes no network requests. Ever. There is no code path that opens a
   *network* socket. (It does open one AF_UNIX socket, inside the 0700 data
   directory, solely so a second launch can raise the first window.)
5. A buffer row is never written until the buffer has content.
6. An image blob is written and fsynced to disk *before* the DB row referencing it commits.
7. A DB row is deleted and committed *before* its blob is unlinked.
8. Text is never silently truncated, normalized, or reformatted on save.
9. All timestamps are stored as UTC epoch milliseconds.
10. No user content appears in logs.

Invariants 6 and 7 both deliberately bias toward *orphan blobs* (harmless, GC'd)
over *dangling references* (user-visible corruption).

---

## 3. Concepts

**Buffer** — a container of items. The user-facing unit. Never *has* to be
named: its title is derived from what was first put on it (first note, then
first link, then first image).

> **Amended, 2026-10-06.** This said "never named by the user". A test user
> asked to be able to name one, and the rule that matters is §1's — the user
> should never *have* to think what to call something. So a name is optional
> (F2, or Rename… on the napkin's menu; schema v7, `buffers.name`). It
> replaces the derived title everywhere a title appears — the list, toasts,
> Clean up, export folders — and search finds it, ranking a napkin found by
> name first and showing all of its board. Clearing it goes back to the
> derived title; renaming never moves a napkin in the list, and is undoable.
> Acceptance criterion 9 still holds: nothing ever asks for a name.
>
> The same user expected the list to show what they added *last*. The title
> stays the first thing (§20: a title that followed every addition made
> napkins unrecognisable), and the row's second line now says
> "Latest: …" — the top of the board, skipping the item the title came from —
> with the count and age kept whole at the right. Rows stay one fixed height.

**Item** — one piece of captured content. Type is `text` or `image`.

**Pin** — affects *placement*. Pinned buffers sit at the top.

**Keep** — affects *lifecycle*. Kept buffers are never swept.

> ### Rename: Lock → Keep
> v1 called this "Lock" and then had to spend a section explaining that lock is
> not encryption. If a name needs a disclaimer, it is the wrong name — a padlock
> icon promises security Napkin does not provide. **Keep** is honest and pairs
> cleanly: *Pin keeps it at the top, Keep keeps it forever.*

Pin and Keep remain fully orthogonal. All four combinations are valid.

> ### Rename: Buffer → Napkin, for users only
> "Buffer" is a programmer's word, and the people this is for would reach for a
> napkin, not a buffer. Everything the user reads says **napkin**: "New
> napkin", "Search your napkins", "Napkin moved to trash". Contents sit *on* a
> napkin, so "Paste onto this napkin", not "into".
>
> The code, the schema and this document keep **buffer**. Renaming `Buffer`,
> `BufferService` and the `buffers` table would touch everything for nothing a
> user could see, and the export manifest's `"buffer"` key is a file format, not
> wording. Where the app's name and the noun would meet ("Napkin could not save
> this napkin"), the sentence is rewritten rather than left to stutter. The
> trash keeps its name: deleting should read as deleting.

### Links are not a type

A URL is stored as a `text` item. A text item whose trimmed content is exactly
one URL is *rendered* as a link chip with Open / Copy actions. URLs inside prose
are linkified at render time.

This is derived presentation, consistent with "preview is derived data." It costs
one function, survives any future change to URL-detection rules with no
migration, and gives URL search for free via the text index.

**Only http and https.** `file:`, `data:`, `javascript:` and every other scheme
are not links — not hidden behind a confirmation, simply never rendered as a
chip and never opened. A scratch surface holds whatever was on the clipboard,
and Open hands it to the desktop. The scheme is checked in `links::isOpenable()`
and again at `ItemCanvas::openUrl()`, the call that actually starts a browser:
the second check costs nothing and the alternative is trusting a caller.

**The chip names the host `QUrl` resolves, never the raw string.**
`https://bank.example@evil.example/` reads as bank.example to anyone skimming
it, and a chip repeating that would lend the deception its own credibility.

**The chip steps aside for editing.** The item is text; nothing may stand
between the user and it. A card being edited keeps the chip's height rather than
shrinking to the text's, so clicking into a link card does not make it jump size.

**`BoardLayout` consults the same rule.** The board measures heights from item
data and never constructs a card, so `links::soleUrl()` is called there too.
Teaching only `TextItemCard` about chips drew every one of them into a card
sized for a single line of text.

URLs inside prose are underlined *and* coloured — §14 forbids meaning carried by
colour alone — and followed with **Ctrl+click**. A plain click still selects the
card, because that is what a click means on every other card.

---

## 4. Input

Exactly three ways content enters Napkin:

1. **Typing** into an expanded buffer.
2. **Paste** (`Ctrl+V`) — the clipboard is inspected in this fixed order:
   ```
   1. the richest decodable image the source offers, kept BYTE FOR BYTE
        vector           image/svg+xml
        animation-capable  image/gif, image/apng, image/webp, image/avif
        static raster    image/png, image/jxl, image/heif, image/jpeg, …
   2. any other image representation  →  transcoded to PNG (lossy; last resort)
   3. text/plain                      →  text item
   4. anything else                   →  ignored, quietly
   ```
   That preference order is a contract. Test it. Two ambiguous cases are real
   and both are covered: copying an image from a browser offers a bitmap *and* a
   URL, and the image wins; a source offering a GIF *and* a PNG must resolve to
   the GIF, or the animation is silently flattened to one frame.

### Image formats

Napkin stores every format it can decode **verbatim** — nothing is re-encoded,
so animation, vector geometry and original quality all survive, and export hands
back exactly what arrived. Only bytes nothing can read are refused.

What "can decode" means is **discovered at runtime** from
`QImageReader::supportedMimeTypes()`, never hard-coded: it depends on which Qt
image plugins are installed, and a missing plugin degrades to the next-best
representation rather than failing.

| Source | Formats |
|---|---|
| Qt built-in | PNG, JPEG, GIF, BMP, ICO, PPM/PGM/PBM, XPM, WBMP |
| `qt6-imageformats` | WebP, TIFF, JPEG 2000, MNG, ICNS, TGA |
| `qt6-svg` | SVG, SVGZ |
| `kimageformats` | **AVIF, HEIC/HEIF, JPEG XL**, PSD, XCF, EXR, DDS, QOI, RAW (CR2/NEF/ARW/DNG/…), and more |

Packaging must list `qt6-svg`, `qt6-imageformats` and `kimageformats` as
recommended dependencies. Without them Napkin still runs; it simply understands
fewer formats, and says so instead of pretending.

**Animation.** A multi-frame image is detected once at import and recorded in
schema v3 (`items.animated`) — reopening the file on every repaint would mean a
file open per visible card per frame. It plays:

- in the **lightbox**, always, decoded frame by frame so a long animation never
  sits in memory whole;
- in its **card, while the pointer is over it** — one at a time. Animating every
  visible GIF at once would spend §12's idle-CPU budget on decoration. A card
  showing a still first frame carries a small `GIF` badge so it is not mistaken
  for a static image.

> **Known limitation, not a defect.** Whether a paste preserves animation is up
> to the *source*. Browsers commonly rasterise to `image/png` on "Copy Image",
> in which case the clipboard never contains the animation and Napkin cannot
> recover it. Copying the file itself, or using **Add image…**, keeps the frames.

**SVG is sanitised at import, because rendering it is *not* inherently safe.**

An earlier version of this section claimed, "measured, not assumed: Qt's SVG
renderer loads no local file references." **That claim was false, and the probe
behind it was written in a way that could not fail** — it tested only
`xlink:href='file:///…'`, the one spelling Qt already rejects. A bare filesystem
path loads fine:

```
xlink:href='file:///tmp/secret.png'   → not read
href='/tmp/secret.png'                → LOADED
xlink:href='/tmp/secret.png'          → LOADED
```

A hostile SVG could therefore render any local image the user can read into a
card — and the thumbnailer would persist a copy of it inside Napkin's own data
directory. Napkin now **refuses at import** any SVG whose `href`, `src` or
`url(...)` points anywhere but a `data:` URI or an in-document `#fragment`, and
refuses compressed SVGZ outright since it cannot be inspected without
decompressing it.

What *did* hold under test: `http://` references trigger no connection, and XXE
(`<!ENTITY SYSTEM 'file:///etc/passwd'>`) is blocked. **Invariant 4 itself was
never breached.** The lesson is about the test, not the renderer: a probe that
only exercises the case you expect to pass is not evidence.
3. **Add image…** (`Ctrl+Shift+I`) — a native file dialog whose filter is built
   from the formats this build can actually open, which copies the chosen file
   into the blob store immediately. This is the reliable path for animated GIFs.

> **On keeping the picker.** You cut *files as a content type*, not *ways to get
> an image in*. Without the picker, a user with `diagram.png` on disk has to open
> an image viewer and copy it — a dead end for no gain. The picker introduces no
> new item type, no MIME handling, no large-file policy, and no lifecycle. It is
> roughly 30 lines. Strike it if you disagree; nothing else depends on it.

There is no drag-and-drop, no generic file import, and no "attach file."

---

## 5. Data model

SQLite, WAL mode, `foreign_keys = ON`, `synchronous = NORMAL`.

```sql
CREATE TABLE buffers (
  id           INTEGER PRIMARY KEY,
  created_at   INTEGER NOT NULL,           -- UTC epoch ms
  modified_at  INTEGER NOT NULL,
  pinned       INTEGER NOT NULL DEFAULT 0,
  kept         INTEGER NOT NULL DEFAULT 0,
  deleted_at   INTEGER                     -- NULL = live; set = in trash
);

CREATE TABLE items (
  id           INTEGER PRIMARY KEY,
  buffer_id    INTEGER NOT NULL REFERENCES buffers(id) ON DELETE CASCADE,
  position     INTEGER NOT NULL,
  type         TEXT    NOT NULL CHECK (type IN ('text','image')),
  created_at   INTEGER NOT NULL,

  text         TEXT,        -- type='text'
  blob_hash    TEXT,        -- type='image', sha256 hex
  source_name  TEXT,        -- original filename if from picker; NULL if pasted
  width        INTEGER,
  height       INTEGER,
  byte_size    INTEGER
);

CREATE INDEX idx_items_buffer   ON items(buffer_id, position);
CREATE INDEX idx_buffers_recent ON buffers(deleted_at, pinned, modified_at DESC);
```

### Image storage

Content-addressed, no refcount table. Images are kept **byte for byte as they
arrived** rather than normalised to PNG: re-encoding a JPEG photo would inflate
it several times over and add generation loss for nothing. Schema v2 therefore
carries a `mime` column, and the blob keeps its own extension.

```
~/.local/share/napkin/napkin/
├── napkin.db
├── napkin.db-wal
├── blobs/
│   └── ab/abcdef0123….png      (or .jpg, .webp, .gif — verbatim)
└── thumbs/
    └── ab/abcdef0123…_96.png
```

Only formats Napkin cannot serve directly are converted, and only on the way in.
Anything above 64 MB is refused with a readable message rather than swallowed.

Pasting the same screenshot twice dedupes for free. On item delete, unlink the
blob only if `SELECT 1 FROM items WHERE blob_hash = ? LIMIT 1` returns nothing.
A query, not a counter — so no refcount drift is possible.

Directory mode `0700`, DB file `0600`. A scratch surface *will* contain tokens
and passwords in practice.

### Theming

Three choices: follow the system, Light, Dark. The two explicit ones build a
**complete** palette — every role a widget can draw with, plus the Disabled
group — rather than patching a few roles onto the platform's.

Patching was the original design and it was wrong in a way that only showed up
when the user's desktop disagreed with their choice. `applyTheme()` set Window,
Base, Text and WindowText and inherited the other sixteen roles, so a dark
desktop with Light selected kept `ButtonText` at white: the menu bar, the header
buttons and the start page's shortcut rows were white text on a light window,
measured at 1.00:1. The mirror-image fault applied to Dark under a light
desktop. A palette is every role or it is none of them.

The one thing still inherited is `QPalette::Highlight` — the accent belongs to
the user, and a saturated accent reads against either background. Its partner
`HighlightedText` is chosen here rather than inherited, because a light theme's
highlighted-text colour carried into a dark theme is how selected text
disappears.

**Tint the role a widget paints with, not the one you assume it paints with.**
A `QLabel` inherits its parent's foreground role, so the labels inside the
start page's clickable shortcut rows draw with `ButtonText`, not `WindowText`.
`applyPalette()` set `WindowText` on them, which was a silent no-op — the rows
took whatever the platform's `ButtonText` happened to be, and the emphasis
difference between a key and its description was lost even when both were
legible. Both `WelcomeView` and `EmptyStateView` now write to
`label->foregroundRole()`.

`tests/test_theme.cpp` pins both faults, starting from a deliberately dark
platform palette: one test measures WCAG contrast for every paired role in both
themes, the other asserts the emphasis tokens actually reached the painting
role. Each fails if its own fix is reverted — checked by reverting them.

### The three empty screens

An empty list is not one state, it is three, and they mean different things:

| When | What it says | The way out |
|---|---|---|
| Nothing stored yet | `WelcomeView`: mark, name, tagline, the five keys that get you started | every row is clickable and runs its action |
| Trash, nothing in it | `EmptyStateView`: the bin illustration, "The trash is empty", the retention period | **Back to your buffers** |
| Search, no hits | `EmptyStateView`: the query echoed back (truncated at 42 characters so a pasted paragraph cannot become the headline) | **Clear search** |

The first-run screen is only for the first case. Greeting someone who has just
emptied the trash as though they had never used the application would be wrong,
and a bare line of centred text in the other two leaves you on a screen with
nothing to do and no obvious way back — so each carries one action, and both of
them call `MainWindow::goHome()`, the same slot the Napkins ▸ All napkins menu item
uses. One implementation, so the menu and the buttons cannot drift apart.

The illustration is the user's own PNG, trimmed of its transparent margin and
rendered at two sizes (`resources/icons/trash-empty-{128,256}.png`), compiled
into `napkin_ui` rather than into the executable so that every consumer of the
library — including the tests — can load it. A test asserts the pixmap is
non-null: a missing resource is an invisible label at runtime, not a build
failure, so nothing else would catch it.

### Search

```sql
CREATE VIRTUAL TABLE items_fts USING fts5(
  text, source_name,
  content='items', content_rowid='id',
  tokenize='unicode61 remove_diacritics 2'
);
```

Kept in sync by `AFTER INSERT/UPDATE/DELETE` triggers on `items`. Results roll
**up** to buffer level and dedupe — the UI shows buffers, so a match on item 3's
`source_name` surfaces the whole buffer. Rank with `bm25()`, snippet with
`snippet()`. No query language in v1; substring-ish prefix matching only.

### Enforcing invariant 1 below the application layer

```sql
CREATE TABLE napkin_meta (key TEXT PRIMARY KEY, value TEXT);

CREATE TRIGGER guard_kept_delete BEFORE DELETE ON buffers
WHEN OLD.kept = 1
 AND COALESCE((SELECT value FROM napkin_meta WHERE key='allow_kept_delete'),'0') <> '1'
BEGIN
  SELECT RAISE(ABORT, 'refusing to delete a kept buffer');
END;
```

The confirmed-delete path sets the flag inside its transaction and clears it
after. This is what "must be enforced by the data layer, not the UI" actually
looks like — a bug anywhere in the service layer cannot destroy kept data.

Migrations from commit one, via `user_version`. Forward-only.

---

## 6. Lifecycle — the decision v1 never made

v1 was contradictory: it declared at length that kept buffers survive "automatic
cleanup," while also showing cleanup as a manual dialog and promising to tolerate
5000 buffers forever. The unanswered question — *does anything ever delete
without being asked?* — made the whole cleanup phase unbuildable.

**Decision: Napkin never auto-deletes a live buffer. There is no expiry.**

Instead, age changes *visibility*, not existence:

```
PINNED    pinned buffers, newest first
RECENT    everything modified within the last 30 days
OLDER     collapsed section, dimmed, still searchable, still there
```

Cleanup ("Sweep") is always user-initiated, surfaced by a quiet inline nudge
once the buffer count crosses a threshold:

```
┌──────────────────────────────────────────────┐
│ 83 buffers · 61 older than 30 days           │
│  4 kept — excluded                Review  ✕  │
└──────────────────────────────────────────────┘
```

Sweep moves buffers to **trash**, it does not erase them. `kept` buffers are
excluded from the sweep's default selection, permanently — that is what Keep
buys you: you decide once, and never re-decide on any future sweep.

### Trash and undo

v1 had no undo anywhere, while also declaring "never silently discard user
content." For an app whose premise is *throw things in without thinking*,
accidental deletion is the single fastest way to lose a user forever.

- Delete is a soft delete (`deleted_at`), always, for every path — items too:
  deleting items inside a napkin moves them into a napkin of their own, which
  goes straight to the trash (`BufferService::trashItems`), remembering where
  they came from (`restores_to`, schema v6): restoring it puts them back into
  that napkin if it is still live. Deleting *every* item trashes the napkin
  itself, content and all.

> **Corrected.** "For every path" was false until 2026-09-19. Napkins were
> soft-deleted; items were hard-deleted, recoverable only from the 8-second
> toast. A first-time user in a usability test deleted their edited reminder,
> missed the toast, found the trash held only napkins ("Deleted napkins stay
> here for 30 days") and lost it. Items now go to the trash the same way, as a
> napkin rather than as a second kind of trash entry, so there is still one
> place to look and one retention rule. The exception is a text card the user
> *emptied*: nothing is left to recover, so it is discarded as before.
- An **Undo** toast appears for ~8 seconds after any delete or sweep. Offers
  **stack**: each lives out its own eight seconds, the toast shows the newest
  and how many are behind it ("· 1 more to undo"), and Undo / `Ctrl+Z` works
  back through them newest first. Each names the napkin it happened to. Each
  offer holds the blobs its undo would need, and the blob sweep leaves the
  union of every live offer's blobs alone.

> **Corrected, 2026-10-05.** Until then a new offer replaced the one showing:
> two deletes in quick succession left only the second undoable, and the
> blobs the first was protecting stayed pinned until the survivor expired.
> A cut finished by a paste now withdraws only its own offer.
- Trash is browsable and restorable, and can be emptied on demand — a confirmed,
  irreversible action, which then reclaims the blobs those buffers held.
- Trash purges items older than 30 days on startup. **This is the only automatic
  hard delete in Napkin, and it only ever touches things the user already deleted.**
  Emptying the trash skips any buffer still marked kept, which is the safe failure.
- Deleting a `kept` buffer requires explicit confirmation, even into trash.

---

## 7. UI

Single window. One vertical stack. Virtualized from day one — §12 promises 5000
buffers, and retrofitting virtualization into a card list is miserable.

### Editing: a second pane

**This reverses the decision the previous two versions of this section argued
for, and the reversal is the right call.**

§7 previously chose inline expansion over master-detail, and rejected a two-pane
mockup on the grounds that two panes add a navigation model. That argument was
sound when a buffer was a note. It did not survive buffers holding four images
and needing item-level operations:

- A card sized for a two-line preview cannot host a 900×520 screenshot, so the
  implementation showed a 52px centre-crop — which is how "images do not look
  very good" happened. It was a layout problem wearing a rendering problem's
  clothes.
- The independent review reached the same conclusion unprompted: *"multi-item
  buffers and inline expansion are in tension, and the spec never reconciled
  them."* It also pointed out that the built app had already paid the
  navigation-model cost — a list focused separately from an editor that stole
  focus, nested scroll regions, keys that silently changed meaning — while
  getting none of the benefit.
- Selecting an *item* in order to copy, cut or delete it needs somewhere for
  items to be objects. A card has no room to be a canvas.

```
+----------------+--------------------------------------+
| PINNED         |  [ text block                      ] |
|  [ card ]      |  [ image, at pane width, captioned ] |
| RECENT         |  [ text block                      ] |
|  [ card ]      |  [ image                           ] |
|  [ card ]      |  [ composer: type or paste…        ] |
+----------------+--------------------------------------+
   selection                  the selected buffer
```

**What the mockup got right, and what it got wrong, both stand.** The list keeps
sections, relative timestamps, thumbnails and the pin/keep indicators — the
things the earlier review correctly said a bare rail would lose — and the list
is still one row per *buffer*, not per item. Only the editing surface moved.

Selection *is* opening: there is no expand step, so a single click both selects
the row and fills the canvas. Enter or double-click puts the caret in the
canvas.

### A board of cards, not a page

Reference: `docs/UI_cards_reference.png`.

Napkin is temporary storage, not an editor. The canvas is a **masonry board**:
uniform column width, each card its own height, newest first.

| Decision | Why |
|---|---|
| **No composer.** | A trailing "write something" box assumed writing is the primary act. It is not — pasting is. `Ctrl+T` summons a card when you do want to type — and so does simply typing on an empty napkin, or on the start page, or double-clicking empty board space. |
| **`Ctrl+N` shows an empty board**, not a blank page | A new buffer is somewhere to paste into. Saying "Nothing here yet · Ctrl+V" is what it is for. |
| **Emptying a text card deletes the item** | An item holding nothing is not a thing, and a blank card is litter. If it was the last item, the buffer goes too. |
| **Newest first** (schema v4, `items.modified_at`) | On a scratch surface the thing you just put down is the thing you want. "Newest" has to mean edited as well as added, or amending an old note leaves it buried. |
| **Column-balanced flow, not a grid** | A grid forces a common height and crops the tall ones; a single column gave a three-word note a 780px row. Each card goes to whichever column is currently shortest. |
| **Cards clip at 420px with a fade** | Past that one phone screenshot owns the board. The fade says "there is more"; double-click opens it. |

Column width is 280–400px: as many columns as fit, then widened to share the
space, so a card is never cramped and never stretched merely because the window
is large.

> The card architecture is deliberately open. `ItemCard` is a base class with
> `heightForColumn()` and `asPlainText()`; text and image are two subclasses.
> Link previews, code snippets with syntax colouring, and map coordinates are
> further subclasses and nothing else has to change.

### The type scale is ratios, not point offsets

Every size used to be `base ± n points`, which is a fixed *proportion* only at
one base size. A −1.5pt caption is 15% smaller at 10pt and 6% smaller at 24pt,
so at the 150% and 200% text settings the card collapsed into one
undifferentiated size — and the people who need large type are exactly the
people who need hierarchy most. Point offsets also do not survive a change of
typeface, where point sizes are not comparable between faces.

| step | ratio | used for |
|---|---|---|
| `kTypeMicro` | 0.70 | the GIF badge — a glyph, not prose |
| `kTypeCaption` | 0.85 | timestamps, captions, section labels |
| `kTypeBody` | 1.00 | everything the user wrote |
| `kTypeLead` | 1.15 | the line that leads a small block, e.g. a chip's host |
| `kTypeTitle` | 1.45 | an empty state's headline |
| `kTypeDisplay` | 2.40 | the wordmark, once, on first run |

`scaledBy()` takes a ratio; `scaled()` survives for the few places that want an
absolute nudge. There were eight ad-hoc offsets before this, including a `+0.5`
that was imperceptible as a size.

### A wide board widens its columns, it does not add more

Columns were packed at `kCardMinWidth`, so the reading measure got **worse** the
bigger the window was: 1920px gave five 280px columns of about 34 characters,
while 1280px gave 417px columns. Columns are now aimed at `kCardTargetWidth`
(360) and then widened to fill, so 1920px gives four columns of ~364px. A board
is for reading, and a wide screen should not be punished for being wide.

### The window opens on two columns

The opening size was `560 x 760`, which the splitter's own minimums silently
raised to about 605. At that width the board draws **one** column, so the first
thing a new user saw was a single stack of notes — which is the thing §1 says
Napkin is not. A board only reads as a board once there are two of them.

The opening width is therefore derived rather than chosen:
`tokens::boardWidthForColumns(2)` inverts the arithmetic in
`BoardLayout::rebuild()`, `ItemCanvas::widthForColumns()` adds the scrollbar and
frame that `stableWidth()` takes away, and `MainWindow` adds the list and the
splitter handle — 1162px on this machine. The divisor is `kCardTargetWidth`, not
`kCardMinWidth`: a board wide enough for two 280px columns still draws one, so
sizing off the minimum would have promised a second column and not delivered it.
`test_canvas` asserts both halves — that the width yields two columns and that
one pixel less yields one — so a helper returning a merely generous number fails.

This is the **opening** size only. The minimum is unchanged at one column plus
the list, so the window can still be dragged narrow or tiled to half a small
screen; it just refuses to *start* there. It is also clamped to the screen's
available width, because a 1162px opening size on a 1024px display is a window
with its edge off-screen. Qt's offscreen platform reports an 800px screen, so
headless runs take the clamped branch — which is why the test asks for the
width again rather than reading it off the window.

### A stylesheet freezes a widget's palette

`QPlainTextEdit` inside a text card used to get its transparency from
`setStyleSheet("background: transparent")`. Applying a stylesheet makes Qt
resolve a palette onto that widget once and the widget then stops following
application palette changes — so switching theme left every card's text at the
old theme's colour, measured at **1.10:1**, black on a dark card. Clicking
another buffer rebuilt the cards and appeared to fix it, which made it look like
a refresh problem rather than a colour one.

Transparency now comes from the palette, and `TextItemCard::applyPalette()`
re-derives it on every palette change like every other colour in the app. The
rule: **no stylesheets on anything that has to follow the theme.** If a widget
needs one, it also needs an `applyPalette()` that puts the colours back.

### The visual system

Tokenised in `src/ui/Tokens.h` rather than scattered as literals, so contrast is
a property of the system instead of a thing each call site gets right or wrong.
Every readable alpha is a measured threshold over `QPalette::Base` against
Breeze Light, the harsher theme: **255** body, **170** secondary (5.06:1),
**161** tertiary — timestamps, captions, section labels **and placeholders**,
which are text and get no exemption. **126** is the floor for a non-text
affordance carrying meaning; anything at or below 90 is decorative and never the
sole indicator of a state.

- *Superseded 2026-10-06 (see "The mockup" below): a resting card has no edge;
  its lighter surface against the window is the edge. The rest of this bullet
  is the history of why cards have surfaces at all.*
- **A card is a card.** Own surface (`Base`), own edge, 16px inset, 10px radius.
  The edge is `Text` **α128 light / α108 dark** (3.09:1 and 4.00:1) — the same
  3:1 floor everything else here obeys. An earlier value of α62 was described as
  "quiet", measured **1.63:1**, and contradicted the constant four lines above it
  in the same file. Selection changes the border's *width* as well as its colour,
  so no state is carried by colour alone. An earlier build drew text cards with no
  border and no fill on the reasoning that "a text item is content, not a
  widget" — which was true about the *content* and wrong about the *card*. The
  board read as loose text rather than as things you can pick up, and that is
  what the user was reacting to. A gutter rail was standing in for the edge; with
  a real edge it is redundant chrome, and it is gone.
- **The board is `Window`; the cards are `Base`.** The desk, and paper on it. A
  card needs something to sit against, so the two cannot both be `Base`. Still no
  gradient and no shadow anywhere.
- **Every card carries a footer**: a one-click *Copy text* / *Copy image* on the
  left, the item's age on the right. The button duplicates `Ctrl+C` deliberately
  — `Ctrl+C` acts on the *selection*, so it needs a selection first, while the
  footer is the one-click path for "give me that one thing". Two mechanisms for
  two intents, not one job done twice. Using it does not disturb the selection.
> **Measure against your own document, not the editor's.** `QPlainTextEdit` uses
> `QPlainTextDocumentLayout`, which ignores `setTextWidth` and wraps to the
> *viewport's current width*, then reports its height in **lines rather than
> pixels**. A card measured at construction — before its viewport has a width —
> therefore came back as one line, which is why a freshly pasted paragraph
> arrived as a single scrollable line. Height is now measured against a plain
> `QTextDocument`, which honours `setTextWidth` and answers in pixels before the
> widget has ever been shown.

- **Sizes are per-card and absolute.** Min 88px tall — one line plus chrome, and
  deliberately not padded to something rounder, because a minimum that exceeds
  what a short card needs makes the board lie about how much is in it. Max 420px
  so one screenshot cannot own the board. Columns 280–460px, floored at the
  minimum rather than squashed below it; a window too narrow for one full card
  scrolls horizontally, which is visible, instead of silently breaching the
  stated minimum.

### Five defects an independent audit found

A subagent rendered 21 states, read every one as an image and probed the pixels.
These five it called "not a matter of taste"; all are fixed.

1. **A thumbnail hung outside its own row.** `collapsedHeight()` derived the
   row from text metrics alone — 35px of content area for a 52px thumbnail, so
   it overflowed the card and painted over its own bottom border, on the first
   thing in the window. A row carrying a thumbnail is now tall enough for one.
2. **Every list border rendered at half strength.** `BufferCardDelegate` built
   its path from *integral* coordinates, so a 1px antialiased stroke straddled
   two rows of pixels and each got half the coverage: the list measured
   **1.67:1** where the board's identical token measured 3.3:1. The colour was
   right; the geometry was throwing half of it away. This is the same fault §7
   already records fixing once, shipping again three files over — which is the
   argument for the half-pixel inset being a rule rather than a remembered
   detail.
3. **A clipped card cut a line of type in half**, leaving severed ascenders
   above the footer. Clipped text cards are now trimmed to a whole number of
   lines so the cut lands in the leading. (The fade §7 once promised is still
   gone — `CardFooter` explains why, and `more…` is the affordance.)
4. **A selected card drew two accent rings 1px apart.** Clicking makes a card
   both selected and current, so the common case was a solid accent edge with a
   dotted accent ring just inside it — read as an artefact, not a selection. The
   focus ring now draws only when the selection is not already saying so.
5. **The trash left the previous buffer's cards on the board.** A mode next to
   deletion that looks identical to ordinary working is the worst kind of
   ambiguity about which one you are in. Switching modes clears the board.

### The board is virtualized

> **Virtualization has a cost the design has to pay back.** Heights come from
> the item data rather than from widgets — which is what makes it possible — but
> item data goes stale the moment someone types into a card. Two things keep it
> true: the live text is pushed back into the board's copy on every keystroke,
> and the measurement cache is invalidated for that item. Without the first, a
> card never grew as you pasted into it; without the second, an *existing* card
> never grew either, because the cache is keyed on the item's last **saved**
> time and that does not move while you are typing.


Card heights are computed from the **items**, not from widgets: text against one
shared `QTextDocument`, images from the dimensions already stored in the row. So
the whole board's geometry is known without constructing anything, and only the
cards inside the visible band plus an overscan actually exist.

**The board is not its cards, and index space is the whole napkin.** This is the
invariant virtualization imposes on everything else, and it was not held:

> **Corrected.** Every selection verb was written against `cards_` — the band —
> rather than `items_`. Measured on a 60-item napkin whose band held 13:
> `Ctrl+A` selected **13**, `Ctrl+C` copied those 13 while reporting a confident
> count, and `Ctrl+A` then `Delete` destroyed 13 and left **47** items behind
> without a word. `cursor_` was worse than wrong, it was *two* things:
> `syncVisibleCards()` read it as a board index while `applySelection()` wrote a
> band index, so the focus ring drew on the wrong card whenever the band did not
> start at the top. `End` stopped at the last card that happened to exist.
>
> Selection, the cursor and copying are now computed over `items_`; `cards_` is
> only what gets *drawn*. `liveCardAt()` returns null for an item outside the
> band and callers cope, `plainTextFor()` falls back to the item when there is
> no card to ask, and `setCursorTo()` scrolls by the board's slot geometry so it
> can move to an item whose widget does not exist yet.

| 1000 text items in one buffer | before | after |
|---|---|---|
| Live `QPlainTextEdit` widgets | 1000 | **12** |
| Peak RSS | 365 MB | **53 MB** |
| Per resize event | 270 ms | **31 ms** |
| Opening the buffer | 357 ms | 165 ms |

Memory is now flat in the item count — 52 MB at 50 items and 53 MB at 1000.
Opening still scales, because measuring a thousand documents is real work; that
is a one-off per buffer and a reasonable next target, not a correctness problem.

> §12 previously claimed virtualization was "Phase 2 architecture, not Phase 10
> polish", and argued that "retrofitting virtualization into a card list is
> miserable" — and then the board was built with none of it. The argument was
> right and the code ignored it. Retrofitting it was indeed miserable.

> **A card's size depends on its own content and nothing else.** That is harder
> than it sounds. The layout width is computed from the widget width **minus the
> scrollbar extent, unconditionally** — because with an as-needed scrollbar,
> adding one item makes the bar appear, shrinks the viewport by ~14px, changes
> the column width and resizes *every card in the buffer*. Both halves are
> asserted: a tall neighbour must not change a short card's height, and adding
> twelve items must not change the width of the card that was already there.
- Images draw at the column width, **never upscaled**, with one caption line:
  filename or format, dimensions, size, and *animated* when it moves.

### Items are selectable objects

The hard part is that a text block must be both a selectable object and an
editable field. Resolved by making a bare click mean the obvious thing for what
is under it:

| Gesture | Meaning |
|---|---|
| Single click | **select** the item, whatever it is |
| Double click | edit it (text), or open the lightbox (image) |
| `Enter` on a selected block | edit it |
| Ctrl / Shift + click | extend the selection |
| `Esc` while editing | stop editing, keep the block selected |
| Click empty canvas | clear the selection, focus the composer |
| `Ctrl+A` | select every item (not the unwritten composer) |
| `↑` `↓` | move **within the column**, to the card above or below |
| `←` `→` | cross to the neighbouring column, landing on whichever card lines up |
| `Tab` | the next card in the focus chain — the "visit everything once" route |

**Arrows walk the board as it looks, not as it was written.**

> **Corrected.** They used to step *document order*, with `Down` and `Right`
> both meaning `+1`, justified as "masonry makes a spatial walk unpredictable".
> The premise was mistaken. `rebuild()` deals every card into an explicit
> column and `Slot::rect.x()` encodes which, so "the card below" is exact
> geometry, not a nearest-neighbour guess. What the rule actually produced,
> measured on a three-column board: the cursor started top-left on TEN, and two
> `Down` presses landed on **EIGHT, top-right** — `Down` travelled along the top
> row once per column before it ever descended, and the card directly beneath
> the cursor was unreachable by any key. It got worse the wider the window.
>
> `BoardLayout::neighbour()` answers it now. Up/Down take the adjacent slot in
> the same column; Left/Right cross one column and pick the card with the most
> vertical overlap, falling back to the nearest centre when nothing overlaps —
> ragged columns mean the answer is rarely the same row index. **The bottom of a
> column is the end**: no wrap to the next column's top, because that jump
> across the whole board is the genuinely unpredictable move the original
> comment was right to fear. Left/Right is how you change column. On a
> one-column board this is identical to the old behaviour, so narrow windows are
> unchanged.

**Three states, three treatments.** At rest: hairline border. Selected: accent
border at 2px plus a faint tint. **Editing: accent border and no tint at all** —
a wash behind text you are actively reading and typing is the thing that makes
it unreadable, and the border plus the caret are two signals already. Editing
also clears the selection, so the two can never stack.

**Selecting in the canvas moves the keyboard there.** Without that, clicking a
card left focus on the buffer list, so `Delete` was delivered to the *list* —
which trashes a whole buffer. And because a delete destroys the card that had
focus, the canvas takes focus back after every one; otherwise the first `Delete`
worked and the second went nowhere.

> **Corrected.** An earlier build put the caret straight into text on a single
> click, on the reasoning that typing is the primary act. That made a text block
> the one thing in the canvas the mouse could not select or delete — you could
> click an image and press Delete, but not a paragraph. Selection is now
> uniform: a block is read-only until you ask to edit it, so "click it, then
> delete it" works on text exactly as it works on an image. Only one block edits
> at a time, so the canvas never has two carets or an ambiguous `Ctrl+C`.
>
> The trailing composer is the single exception: it is empty and has no row, so
> selecting it would mean nothing. Clicking it just starts writing.

`Ctrl+C`, `Ctrl+X` and `Delete` act on the selection when the canvas has focus,
and never while a caret is in a text block — there, they mean what they always
mean. A single selected image copies as an **image**, so it pastes into anything;
any other selection copies as text, joined in document order.

Deleting every item in a buffer trashes the buffer itself: an item-level delete
that leaves an empty husk behind is just litter. Deleting some of them trashes
them as a napkin of their own (§6). Both go through the ordinary undo toast,
which puts items back in their original positions; Cut says "Item cut" rather
than announcing a deletion, though it goes the same way.

**A delete leaves the next item selected**, clamping to the new last item when
you delete off the end — so a run of deletes does not require re-aiming the
mouse between each one.

### Calculating a line

A napkin is where `1200*12` gets worked out, so the editor can work it out.
**Only when asked.** `Ctrl+Tab`, or *Calculate* in the editor's right-click menu
(beside Cut, Copy and Paste, the other things done to the text under the
caret). Plain `Tab` was not available: it walks between cards, and that is how
the board is reached without a mouse.

| What is under the caret | What happens |
|---|---|
| a line ending in `=` — `rent 1200*12 =` | the answer is written after it: `rent 1200*12 = 14400` |
| a line already showing an answer — `13*3 = 36` | the answer is replaced: `13*3 = 39` |
| any other line ending in a calculation with an operator — `12*3` | ` = 36` is appended |
| a selection inside one line | exactly the selection is calculated; the answer goes in the `=` slot after it if there is one, otherwise ` = answer` follows it |
| a selection across lines | every line in it that ends in `=` or shows an answer; nothing else |

Right-click → *Calculate* acts on the line that was clicked. A right-click
outside the selection moves the caret there first, as most editors do.

**The calculation is the run at the end of the line that parses, and that
starts where a calculation can start.** That means the start of the line, the
start after a list marker (`- `, `* `, `+ `, `• `, `1. `, `2) `), or after
whitespace following prose. It never starts straight after a character
(`10:30`, `0x1F`), and never after whitespace that follows a digit, a bracket,
an operator or a currency sign. Any of those means the calculation began
earlier. If the whole of it did not parse, a tail of it is not the answer. It
is refused.

**A bare number is not a calculation**, selected or not. Without that rule
`Chapter 3` would become `Chapter 3 = 3`. **Without an `=`, it takes a binary
operator**, so `I like pi` and `check the log 100` are prose, while `pi =`
asked. A multi-line selection skips lines that did not ask. Selecting a whole
note and recalculating it must not append to `Call 555-1234`, which parses as
a subtraction. On the caret line, `Ctrl+Tab` on that same text was an explicit
request, and it gets `= -679`.

**A limit that stays.** `pages 3-4 = 2` has exactly the shape of an answered
sum, and recalculating a selection containing it gives `= -1`. Nothing in the
text tells the two apart. One undo puts it back.

**What it understands.** `+ - * / ^ **`, `x × ÷ −` as typed on phones, `mod`,
parentheses, `sqrt abs round floor ceil ln log sin cos tan`, `pi π e`, and
exponents (`1e3`). Angles are radians unless written in degrees: `sin 30°`.
A function's argument is one bracket or number, so `log(1000)^2` is 9.
Digits in any script (`५*२`) are read. A number with a leading zero (`007`,
the `01` of `2024-01-15`) or a `0x` prefix is an identifier, not a quantity,
and is refused. A percent works as it does on a pocket calculator:
`200 + 10%` is 220, and `15% of 80` is 12. Grouped numbers keep their grouping,
in either the western `1,234,567` or the Indian `12,34,567` style. A comma that
fits neither is refused, so `1,2` is not silently read as twelve. One currency
symbol (`$ € £ ¥ ₹`) is carried into the answer, and mixing two of them is
refused for the whole line. Answers have twelve significant digits, so
`0.1+0.2` gives `0.3` and `4.35*100` gives 435 rather than float noise. Whole
numbers below 10^15 are written in full. A large amount keeps two decimals, up
to the sixteen digits a double holds: `$1,234,567,890,123.50 + 1` gives
`$1,234,567,890,124.5`. Only past 10^15, or below 10^-9, does an answer go
scientific.

**What it does not do.** No variables, units, dates or currency conversion.
Those are what turn a calculator into a product, and currency conversion
needs the network (invariant 4). Decimal commas (`1,5`) are not read as
decimals, because the comma is already taken by grouping.

**The answer is ordinary text.** Autosave, search, copy and export see exactly
what is on screen. The calculation is one undo step, kept separate from the
typing on both sides of it.

- **Before:** without the edit block, `Ctrl+Z` merged the answer into the
  preceding keystrokes and took the sum away with it.
- **After:** `QTextDocument` joins a lone insert onto an edit block that ended
  with an insert. So typing straight after the answer and pressing `Ctrl+Z`
  took the answer too. A no-op `QAbstractUndoItem` appended last in the block
  is Qt's way to end it.

Both are caught by `test_calculate`, and both were verified by removing the fix.
Recalculating a line that is already right changes nothing, not even the undo
stack. The barrier goes in only when the text changed.

**When there is nothing to do, it says so** at the caret, in a tooltip. The
reasons are *Nothing to calculate on this line*, *Can't divide by zero* and
*That has no answer*. A silent `Ctrl+Tab` reads as a broken key.

**Bounded.** A line over 1000 characters is not parsed. Nesting deeper than 64
is refused rather than recursed into, for the whole line, not just the part
that went too deep. 400 levels of parentheses would otherwise be several
hundred stack frames on a Windows main thread with 1 MB. The deep-nesting test
originally tested nothing, because its input was longer than the length cap
and never reached the parser. It now nests inside the cap, and goes red when
the limit is removed. The slowest line in a 200,000-string fuzz takes 0.03 ms.

> **Corrected by independent review, 2026-09-24.** The first version took *the
> longest run at the end of the line that parses*, from anywhere. That made it
> answer part of what was written instead of refusing all of it:
>
> - `1 000 000 * 2 =` gave 0, and `1 500 + 250 =` gave 750.
> - `(2+3)*(4+5 =` gave 9.
> - `10:30 + 45 =` gave 75.
> - `Meeting 2024-01-15` gave 2008.
> - `0x10` gave 0.
> - A markdown bullet was a minus sign: `- 5 + 3 =` gave -2, including in a
>   whole-note recalculation.
> - It went around its own refusals. `£5 + $5 =` gave `$10`. 101 minus signs
>   before `(1+2)` gave the innermost 64 levels' answer, 3.
> - Separately, `log(1000)^2` gave 6, because a function took the power into
>   its argument.
> - Answers over 10^12 that were not whole numbers went scientific and lost
>   their cents.
> - A selection ending at the start of a line calculated that line too.
> - Right-click → Calculate acted on the caret's line, not the clicked one.
>
> I had written 34 tests and none of them covered any of this; every one was a
> case I had thought of. Each finding is now a test, and each fix was checked
> by removing it and watching its test go red. Re-running the review's own
> inputs then found one more: digits of other scripts (`५`) passed `isDigit()`
> and failed `toDouble()`, and were reported as *That has no answer*.

`src/domain/Calc.cpp` is Core-only and tested headless (`test_calc`). The key,
the menu, undo and saving are tested through the real editor
(`test_calculate`), with the window active, so that a `QAction` bound to the
same chord would be found by the test.

### An action says that it happened

Copying changes nothing on screen: without a word from the application, a
successful copy and a broken one look identical. Napkin answers at two scopes,
and the scope is chosen by who can speak for the action.

| Scope | Surface | Used for |
|---|---|---|
| One card | its own footer — `CardFooter::acknowledgeAction()` turns "Copy text" into "Copied" for 1.6s | a copy of a single item |
| The window | the toast, via `UndoToast::inform()` — no Undo button, because nothing was destroyed | a copy of several items at once |

**A copy that failed never says it worked, and a cut that cannot copy removes
nothing.** One image whose blob has gone from disk is the only case where the
clipboard cannot be written, and both verbs go through the same
`putSelectionOnClipboard()` so they cannot disagree about what success means.

> **Corrected.** The copy fell through to the text path, put the literal
> `[image]` on the clipboard and said "Copied". The cut did the same and then
> **deleted the item anyway** — a move that lost the thing being moved, leaving
> the picture in the trash and the word `[image]` on the clipboard.

A **cut answers with one message, not two.** It used to call `copySelection()`,
which announced "Copied" before the window's own "N items cut" replaced it in
the same call stack — so a single card flashed "Copied" as it disappeared.

An acknowledgement **never cancels a live Undo offer.** `inform()` used to be
spelled `offer(message, nullptr)`, so a copy — which destroys nothing — threw
away the Undo for a delete made two seconds earlier, and `Ctrl+Z` then did
nothing. It now stands in front of the offer for 1.6s and puts it back with the
rest of its countdown.

**The acknowledgement belongs to the verb, not to a route into it.** There are
three ways to copy — the footer button, `Ctrl+C`, and the context menu — and
`ItemCanvas::copySelection()` is what acknowledges, so all three do.

> **Corrected.** This shipped wired to the footer button's *signal*, so only the
> button said "Copied"; `Ctrl+C` and right-click ▸ Copy were silent, which is
> the case a keyboard user hits every time. The test covered the button alone
> and so never saw it. It is now a data-driven test over all three routes, and
> reverting the fix fails the two that were broken while the button still passes.

### One rule for paste

A paste goes into the buffer you are looking at, and makes a new one only when
you are looking at nothing. This holds for text and images alike.

> **Corrected.** Text used to always create a new buffer while an image appended
> to the selected one, so the same gesture did two different things depending on
> what you had copied. `Ctrl+T` adds an empty text block to the current buffer
> by the same rule, and is the keyboard route to what pasting text does.

> **Item removal does not unlink blobs, deliberately.** (Since the trash change
> above, removed items are rows in a trashed napkin, so their blobs are simply
> still referenced until the trash is emptied.) An earlier version
> deleted the rows and reclaimed the files in one step, so undoing inside the
> 8-second window restored a buffer whose items no longer existed — measured at
> **0 items recovered out of 4**. Removal now captures the items, restores them
> at their original positions on undo, and leaves the files alone; the startup
> sweep reclaims them once undo is no longer on offer. The toast takes a closure
> rather than a `BufferId`, so buffer-level and item-level undo share one widget.

> **The freeze applies to ORDER, never to MEMBERSHIP.** Deleting every item in a
> buffer trashes it — but with the order frozen from typing, the reload was
> deferred and the buffer stayed visible in the list while the toast beneath it
> said it had been moved to the trash. Every structural change (trash, restore,
> pin, keep, sweep, undo) releases the freeze before reloading.

**The order freeze survives the change.** Autosave still bumps `modified_at` on
every flush, so the list would still re-sort under the buffer being edited. The
canvas freezes the order on the first keystroke and releases it when the
selection moves on or the window loses focus.

### Theming

Three choices: follow the system, Light, Dark. The two explicit ones build a
**complete** palette — every role a widget can draw with, plus the Disabled
group — rather than patching a few roles onto the platform's.

Patching was the original design and it was wrong in a way that only showed up
when the user's desktop disagreed with their choice. `applyTheme()` set Window,
Base, Text and WindowText and inherited the other sixteen roles, so a dark
desktop with Light selected kept `ButtonText` at white: the menu bar, the header
buttons and the start page's shortcut rows were white text on a light window,
measured at 1.00:1. The mirror-image fault applied to Dark under a light
desktop. A palette is every role or it is none of them.

The one thing still inherited is `QPalette::Highlight` — the accent belongs to
the user, and a saturated accent reads against either background. Its partner
`HighlightedText` is chosen here rather than inherited, because a light theme's
highlighted-text colour carried into a dark theme is how selected text
disappears.

**Tint the role a widget paints with, not the one you assume it paints with.**
A `QLabel` inherits its parent's foreground role, so the labels inside the
start page's clickable shortcut rows draw with `ButtonText`, not `WindowText`.
`applyPalette()` set `WindowText` on them, which was a silent no-op — the rows
took whatever the platform's `ButtonText` happened to be, and the emphasis
difference between a key and its description was lost even when both were
legible. Both `WelcomeView` and `EmptyStateView` now write to
`label->foregroundRole()`.

`tests/test_theme.cpp` pins both faults, starting from a deliberately dark
platform palette: one test measures WCAG contrast for every paired role in both
themes, the other asserts the emphasis tokens actually reached the painting
role. Each fails if its own fix is reverted — checked by reverting them.

### The design system (draft — phase 1 of the own-look work, 2026-10-06)

**Decision.** Napkin keeps Qt Widgets and gets its own look on every platform:
its own palette, one widget style everywhere, and a bundled typeface. It does
**not** change toolkit; §9's reasons — real text editing, IME, AT-SPI — are
untouched by a styling decision. What stays native: the window frame and title
bar (a frameless window loses Wayland/KDE snapping and moving), file dialogs,
the tray, and text input. High-contrast mode overrides Napkin's palette; §14 is
a hard requirement and an own look does not get to break it.

**Why now.** The content was already Napkin's own: cards, list rows, toast,
chips and footer paint themselves from `Tokens.h`. What still varies by
platform is the chrome around it. Rendered offscreen under Breeze and Fusion
(light and dark, 2026-10-06): the board and list are near-identical; the
search field (bordered vs flat), the menu bar (mnemonic underlines under Fusion
only), the splitter handle and a ~9px vertical shift differ; the Settings dialog
differs almost everywhere — group boxes, combo widths, spin arrows, button
icons, alignment. The Windows dark-controls fault (`applyStyle`) was the same
class: a platform style deciding what Napkin looks like.

**Phases.**
1. *Guidelines* (this section): every value a role, every role a token, the
   contradictions below resolved. Look-dependent values wait for the user's
   reference designs (Obsidian / AFFiNE-like) and are marked **OPEN**.
2. *Own palette by default.* Built from tokens; "follow the system" chooses only
   light vs dark. Napkin's accent is the default; the presets stay.
3. *`NapkinStyle`*, a `QProxyStyle` over Fusion on every platform, drawing
   buttons, fields, combos, spin boxes, check boxes, scroll bars, menus, menu
   bar, tooltips, group boxes and the splitter from tokens. Replaces both
   stylesheets ("A stylesheet freezes a widget's palette" already forbids them
   on themed widgets) and `applyStyle()`'s Windows-only switch.
4. *Bundled typeface* with system fallback for CJK and emoji. The board measures
   items by font metrics, so BoardLayout is re-checked, not assumed.

Each phase is rendered light and dark under Breeze and Fusion and on Windows
CI (`test_native_style`) before it is called done.

**Token families.** Today `Tokens.h` covers text alphas, card geometry, two
radii and a type-ratio scale. The system needs:

| Family | Today | Target |
|---|---|---|
| Colour roles | Two hand-built Breeze-derived palettes in `SettingsDialog.cpp`; badge black/white literal | Named roles — canvas, surface, surface-raised, sidebar, border, text ×3, accent, accent-on, danger, focus — per theme, from which the `QPalette` is *derived*. Values **OPEN** |
| Alphas | Text 255/170/161, hairline 36, card border 128/108 | Kept and re-measured against the new surfaces; literals 60, 50, 25, 18, 80, 150, 210, 215, 230 folded into roles or justified |
| Type | Ratios 0.70–2.40 of the system font | Same ratios over a bundled face; weights named (regular, medium, semibold); letter-spacing for the section label and wordmark as tokens. Face **OPEN** |
| Spacing | 4px scale in part; list uses 14/16/4, toast 16/10/14, dialogs 20/12/10 | One 4px scale; every margin a multiple of it |
| Radii | 10, 6 | A short scale covering toast 8, thumbnails 4, badge 3, menu items 4 and the focus ring. Values **OPEN** |
| Strokes | 1px edges, 2px selected, 2px dotted focus ring | Edge, emphasis and focus widths as tokens; the focus ring becomes solid, offset and accent-coloured — the dotted ring is the one dated element in every render |
| Icons | 13, 14, 16, 18px | Two sizes, stroke proportional as now |
| Motion | None; flashes 1600ms, toast 8000/1600ms | Durations as tokens. Any animation added later honours reduced motion |

**Contradictions found and resolved** (2026-10-06 inventory, spot-checked;
fixed the same day). Every visual value they involved now lives in `Tokens.h`:

1. Image edges were a literal α60 in two places while `kHairline` (36) claimed
   to be the image-edge value. 60 is right — the edge must separate a dark
   screenshot from a dark card — so it is now `kImageEdge`, and `kHairline` says
   what it is actually for.
2. The placeholder alpha was `kTextTertiary` written as 161 → the token.
3. The Light and Dark palettes were literals in `SettingsDialog` and `textOn()`
   kept its own copy of two of them → `kLightColors` / `kDarkColors`, from which
   the `QPalette` is built and `textOn()` reads.
4. **The one visible change.** List rows drew the light theme's border alpha
   (128) in both themes and ignored hover; board cards use `cardBorderAlpha()`.
   Rows now use it too, so dark-theme row edges are α108 (4.00:1), as the board's.
5. `kRailWidth` was unused; the list drew a literal 3 → the token.
6. `lightness() > 128` was inlined four times → `isLight()` / `isLightTheme()`.
7. The list's row metrics (margins 16/4, inset 14, label 36, max width 760,
   thumbnails 52/38) moved to `Tokens.h` as `kRow*`, with the reason the row is
   denser than a board card — a decision, recorded, rather than drift.
8. Font floor: 5.0pt in `SettingsDialog`, 7.0pt in `scaledBy` → one
   `kMinPointSize` (7.0). It only bites below 7pt, which the size menu never
   reaches; a corrupt setting now floors at 7 rather than 5.
9. Focus ring: the list wrote inset 3.5, the board 3 on a box already 0.5 in —
   the same ring → `kFocusInset` / `kFocusWidth`.
10. `scaled()` was unused → removed. `kTypeBody` now sets the list's primary
    line. `readableAccent()` uses `contrastRatio()` instead of its own copy.

*Verified:* offscreen renders before and after, Breeze and Fusion, light and
dark, main window and Settings — six of eight byte-identical; the two dark main
windows differ in 2,412 pixels, all on the unselected list rows' edges, nothing
else. `test_theme` gains `aListRowsEdgeFollowsTheBoardsRuleInBothThemes`, which
fails in Dark with the old rule restored (`#8b8d8e` vs `#7a7c7d`) — checked by
restoring it. Full suite passes.

Still literal and still to be folded into roles when the palette is designed:
alphas 18, 25, 50, 80, 150, 210, 215, 230; the GIF badge's black and white;
toast, thumbnail and badge radii; icon sizes; flash and toast durations.

### The mockup (2026-10-06): Napkin's own look, implemented

The user supplied `mockup.png` (1280×832, light) and asked for the UI and UX to
follow it. Measured from the image, then built — phases 2–4 of the design
system above landed together, because the mockup decided the OPEN values.

**What the mockup decided.**
- *Colours.* A warm grey desk (#E0DFDD) with lighter surfaces (#F2F1EF) for
  everything you touch: cards, the sidebar, the search field, the switch, the
  round buttons. Text is darker than the mockup's (#141312) so tertiary text
  (α161) still reaches 4.5:1 on the desk as well as on a card. Dark is the same
  structure: desk #1B1A19, surfaces #262523. `kLightColors` / `kDarkColors`.
- *No edges at rest.* Cards and sidebar rows are surfaces, not boxes. This
  overturns the earlier "the edge is a 3:1 affordance" rule for resting cards;
  the surface step is the boundary, and every *state* still has a shape: hover
  draws an edge (α70), selection a 2px accent edge plus a wash, editing the
  accent edge, keyboard focus a solid accent ring. List rows: the chosen row is
  cut out of the sidebar in the desk colour, plus the accent rail.
- *Header.* Search on the left, exactly as wide as the sidebar (it follows a
  dragged splitter); a Home / Trash switch centred on the window; New,
  Settings and Menu as 38px round buttons. 16px window margin, 24px gaps.
- *No menu bar.* Every action is in one menu behind the menu button, in
  sections (top: create and paste; *Napkins*; *Trash*; then export, settings,
  help, quit). F10 opens it, as on GNOME and KDE; every action with a chord is
  also the window's own action, so chords never depended on a menu bar. A
  side effect worth having: Plasma's Global Menu used to move the bar out of
  the window entirely.
- *Image cards* hold the picture 6px from the edge with rounded corners; the
  caption line (format · size · bytes) is gone and its facts are the tooltip.
  `cardInset()` / `cardChromeHeight(font, image)` are the one rule both the card
  and `BoardLayout` use — the board measures cards without building them.
- *Typeface.* Inter, bundled (SIL OFL, `resources/fonts/inter`), four static
  weights. "Napkin ships no fonts" is no longer true, deliberately. Base size
  is max(10.5pt, the desktop's) so Windows' 9pt default no longer makes every
  card smaller than on KDE, and a desktop set larger is still honoured.

**One style, every platform.** `NapkinStyle` (a `QProxyStyle` over Fusion)
draws the standard controls from the palette, so no stylesheet is needed and
none remains (the menu bar's was the last). The Windows-only "Fusion in dark
mode" switch is gone: the platform style never decides the look now.
Controls are *cut out of what they sit on*: on the desk a field is a surface;
on a surface (a settings group, the sidebar) it takes the desk colour. One fill
for both made every Settings field vanish into its group in the first render.
"Follow the system" now follows the system's light or dark *in Napkin's
colours*; it used to mean the platform palette, so the same Napkin was Breeze
on KDE and grey Fusion on Windows.

**High contrast steps aside.** When the OS asks for high contrast (Qt ≥ 6.10
can say so), Napkin uses the platform's style and palette unmodified. With
Qt 6.8, which CI builds against, the request cannot be read; that gap is known.

**Found while building it, fixed.**
- The opening width forgot the new window margin, so the board opened 16px
  short of its second column (`theWindowOpensWideEnoughForTwoColumns`).
- The search field's magnifier is a button and announced nothing
  (`everyControlAnnouncesItself`).
- Library icons ignored their colour's alpha (`#RRGGBB` written into the SVG),
  so the empty trash's "quiet" glyph rendered full white in Dark.
- A ring drew round the first list row at startup, because the list takes
  focus when the window opens; it now waits for a keyboard focus change.
- The paste-moves-to-top test could tie with a seed in the same millisecond;
  its seeds are an hour old now.

*Verified:* offscreen renders, light and dark, of the main window, menu,
trash and Settings, compared against the mockup by eye and by sampled
colour. New or rewritten tests — `aListRowIsPartOfTheSidebarUntilChosen`,
`theMenuFollowsNapkinsThemeNotTheDesktops`, `everyActionIsReachableFromTheMenu`,
`everyMenuActionIsReachableByKeyboard`, the Home / Trash switch in
`theTrashMenuAndTheHeaderToggleStayInStep`. The first two were checked to fail
with their behaviour reverted (row filled with the surface: `#f2f1ef` for
`#e0dfdd`; menu panel in the desk colour); the others were not break-tested.
Full suite passes.

**Independent review, same day.** A separate reviewer rendered states the
first pass had not — hover, keyboard focus, a control gallery, right-to-left,
200% text, dialogs, popups — and measured them. Fixed:
- *A crash.* The scroll-bar thumb was `clamp(len, 36, barLength)`; under 36px
  the bounds reverse, which is undefined and aborts under
  `_GLIBCXX_ASSERTIONS` — which makepkg sets and our own builds did not. The
  class fix: **every non-MSVC build now defines `_GLIBCXX_ASSERTIONS`**, so the
  tests run the checks the shipped binary runs. `aShortScrollBarStillHasAThumbInsideIt`
  aborts on the old line with assertions on (checked). *Corrected claim:* the
  review said the thumb also came out longer than the bar without assertions;
  with libstdc++ it does not (min(max(v,lo),hi) gives the bar's length) — the
  defect is the undefined call, not a visible one.
- *Keyboard focus was invisible* wherever Qt asks the style for a focus rect —
  the Sweep dialog's list (which has no selection, so the ring was the only
  cue), radios, plain tool buttons. They get the solid accent ring now.
- *Selected rows in dialog lists were unreadable in Dark*: a tinted fill with
  the text still in `HighlightedText` (chosen for an opaque fill).
- *Hover and selection in the sidebar measured 1.02:1 apart.* Hover is an edge
  now; selection keeps its fill, and its rail is 4px in the readable accent.
- *`readableAccent` was 3:1 against Base only*; a selected card's edge sits half
  on the window, where it measured 2.77:1. It now meets 3:1 against both.
- *The Home / Trash switch* let a second click on Trash leave the trash (now an
  exclusive group), and said "chosen" by a 1.18:1 fill alone (now also weight
  and an edge). "Show trash" in the menu is a command, not a checkbox.
- *The menu's active item* was a 1.14:1 tint — the menu is now the keyboard's
  only route to everything — and is the accent with its own text colour.
- *A focused autoDefault button* (Tab to Cancel) was painted as the primary.
- *Fixed sizes ignored the text-size setting*: header controls, segments,
  icons and the check box grow with the font; the trash bar stacks.
- *Contrast:* unchecked check box edge 2.69 → 3.75:1; scroll thumb at rest
  raised; menu edge stronger now that Breeze's shadow is gone.
- *Insets, RTL, popups:* field text 12px from the edge; combo, group box and
  horizontal scroll bar mirror right to left; the combo's list is a rounded
  surface like the menu; header spacing moved to the mockup's (32/32 against
  its 34/35); the search field no longer jumps from 320 to 344px when the
  first napkin appears.

Known and not fixed, deliberately or for now:
- The board's right margin is ~26px against the mockup's 16: `ItemCanvas`
  reserves the scroll bar's width so the layout does not jump when it appears.
- Below ~940px wide the switch drifts off centre (the search field cannot
  shrink below the sidebar's width).
- Image cards keep a hairline edge the mockup lacks — it is what keeps a dark
  screenshot from reading as a hole in a dark card.
- An image's file name is now only in its tooltip, which the keyboard cannot
  reach; showing it in the footer when it exists is the likely fix.
- Text-only list rows keep their text at the top of a row sized for a
  thumbnail.
- Untested here: X11 without a compositor (rounded popups would show black
  corners), fractional HiDPI (half-pixel hairlines may blur), checkable group
  boxes and split-button tool buttons (neither exists yet), and the switch's
  accessible role (two toggle buttons, not a tab list).

**First use by the user, same day, and what changed.**
- *The search field could not be clicked into* — only Ctrl+F reached it. Naming
  the magnifier for screen readers looped over the search action's associated
  objects and set `NoFocus` on each; the field itself is one of them. Ctrl+F
  calls `setFocus()`, which ignores the policy, so every keyboard test passed.
  Now only the glyph's button is touched, and
  `theSearchFieldTakesFocusFromAClickAndFromTab` fails if the field loses click
  or Tab focus again (checked by reintroducing the loop).
- *No accent rail on the chosen napkin* (asked for). Selection is still not
  colour alone: the row is cut out of the sidebar and its title is set
  semibold. `aListRowIsPartOfTheSidebarUntilChosen` samples where the rail was.
- *No trash bar in the sidebar* (asked for). Its note and "← Back to napkins"
  answered a test user who did not see the old one-button Trash toggle as an
  exit; the Home half of the switch is that exit now, always in view. The
  retention period moved to the Trash segment's tooltip.
- *The glossy trash-can picture is back* on the empty trash (asked for). The
  flat glyph that replaced it and its loader are gone.
- *Settings has two panes*, as KDE's System Settings: a page list in a surface
  (Appearance; Napkins & trash; In the background) and the chosen page, each
  with its groups as surfaces. The tray and the global shortcut share a page —
  they are one subject, whether Napkin stays running. The dialog is still
  exactly the size of its largest page.
- *Theme is in the main menu* (Follow the system / Light / Dark), applied at
  once with no dialog; `SettingsDialog::setTheme()` is the one way both the menu
  and the dialog store it.

**Second round of the user's notes, same day.**
- *Padding.* Text buttons outside dialogs are pills, cut out of what they sit
  on, with 16px from the words to the rounded ends (`kPillPadX`): "Empty
  trash…" had 7px, from Fusion's own margin. The toast's and the clean-up
  nudge's flat words-as-buttons are pills now; the nudge's ✕ is a round
  target. Cards and the toast are marked as surfaces, so a control inside one
  (a link chip's Open) is no longer drawn in the card's own colour. A focus
  ring around a radio's or a label's text stands 4px off the letters.
- *The empty states sit at the middle of the window*: the page gets the window
  margin on its right too (it had it on the left only, putting the centre 8px
  off), and a 2:3 stretch above and below lifts the block to the window's
  middle rather than the page's, which is lower by the header.
- *Never "Select a napkin".* Whenever the list has napkins and none is open,
  the latest opens — the most recently changed, or in the trash the most
  recently deleted. `selectLatestIfNone()` follows every model reset (a reset
  clears the current row silently, and a deferred re-sort reloads from inside
  the model where MainWindow cannot see it; Qt's list view then picked its
  *first* row on focus, which for a pinned napkin is not the latest — the test
  caught exactly that). `theBoardNeverWaitsForASelection`.
- *A welcome tour* (`TourDialog`), because Pin, Keep, Calculate, the Trash's
  undo and Older are each a chord or a right-click away and otherwise
  invisible. Six steps — what Napkin is for; paste and copy; Pin and Keep;
  calculating a line; search; the trash, undo, Older and Clean up — plus a
  seventh on the global shortcut only where the desktop can do it. Each has a
  drawn miniature in the theme's colours. It opens once on the first launch
  (from `main()`, so no test window meets a modal dialog), and is in the menu
  ("Welcome tour") and on the start page ("Take the tour"). Every claim in it is
  a tested feature; `theTourWalksThroughAndIsSeenOnce` checks the features are
  named and that skipping counts as seen.
- *Full screen* (asked for): F11, and *Full screen* in the menu, checked while
  on; leaving it returns to maximized if the window was. F11 is added to the
  platform's own binding only when that lacks it — measured: under KDE's
  platform theme `QKeySequence::FullScreen` is Ctrl+Shift+F alone, while on
  other platforms it already includes F11, and one chord listed twice on an
  action is ambiguous and fires nothing. `fullScreenIsF11AndInTheMenu`.
- *"The trash can is empty"* on the empty trash (asked for).


### The three empty screens

An empty list is not one state, it is three, and they mean different things:

| When | What it says | The way out |
|---|---|---|
| Nothing stored yet | `WelcomeView`: mark, name, tagline, the five keys that get you started | every row is clickable and runs its action |
| Trash, nothing in it | `EmptyStateView`: the bin illustration, "The trash is empty", the retention period | **Back to your buffers** |
| Search, no hits | `EmptyStateView`: the query echoed back (truncated at 42 characters so a pasted paragraph cannot become the headline) | **Clear search** |

The first-run screen is only for the first case. Greeting someone who has just
emptied the trash as though they had never used the application would be wrong,
and a bare line of centred text in the other two leaves you on a screen with
nothing to do and no obvious way back — so each carries one action, and both of
them call `MainWindow::goHome()`, the same slot the Napkins ▸ All napkins menu item
uses. One implementation, so the menu and the buttons cannot drift apart.

The illustration is the user's own PNG, trimmed of its transparent margin and
rendered at two sizes (`resources/icons/trash-empty-{128,256}.png`), compiled
into `napkin_ui` rather than into the executable so that every consumer of the
library — including the tests — can load it. A test asserts the pixmap is
non-null: a missing resource is an invisible label at runtime, not a build
failure, so nothing else would catch it.

### Search

```sql
CREATE VIRTUAL TABLE items_fts USING fts5(
  text, source_name,
  content='items', content_rowid='id',
  tokenize='unicode61 remove_diacritics 2'
);
```

Kept in sync by `AFTER INSERT/UPDATE/DELETE` triggers on `items`. Results roll
**up** to buffer level and dedupe — the UI shows buffers, so a match on item 3's
`source_name` surfaces the whole buffer. Rank with `bm25()`, snippet with
`snippet()`. No query language in v1; substring-ish prefix matching only.

### Enforcing invariant 1 below the application layer

```sql
CREATE TABLE napkin_meta (key TEXT PRIMARY KEY, value TEXT);

CREATE TRIGGER guard_kept_delete BEFORE DELETE ON buffers
WHEN OLD.kept = 1
 AND COALESCE((SELECT value FROM napkin_meta WHERE key='allow_kept_delete'),'0') <> '1'
BEGIN
  SELECT RAISE(ABORT, 'refusing to delete a kept buffer');
END;
```

The confirmed-delete path sets the flag inside its transaction and clears it
after. This is what "must be enforced by the data layer, not the UI" actually
looks like — a bug anywhere in the service layer cannot destroy kept data.

Migrations from commit one, via `user_version`. Forward-only.

---

## 8. Persistence and durability

v1 said "debounced persistence" and separately demanded survival of `kill -9`.
Those are in tension: the debounce window *is* the loss window. Concretely:

- **WAL + `synchronous = NORMAL`.** You then lose data only on OS or power loss,
  never on application crash or `kill -9` — which is exactly the test that matters.
- **Debounce 300 ms, hard max-delay 2 s.** Continuous typing still commits at
  least every two seconds.
- **Force flush** on collapse, buffer switch, window blur, and close.
- **Large notes save in the background while you type** (2026-10-05). Saving
  is linear in the note — SQLite rewrites the row and FTS5 tokenises the old
  text out and the new text in: 139 ms for 10 MB, 689 ms for 50 MB — and the
  max-delay made that a regular freeze mid-typing. A *timed* autosave of notes
  that already have rows, over 256 K characters, is written by
  `BackgroundSaver` on a second connection to the same file, one job at a time,
  in order. **Every forced flush first waits for it** and then writes
  synchronously, so collapse, switch, blur and close still mean "the text has
  landed" exactly as before. A card is marked saved only when the write it
  asked for has landed *and* it has not been edited since (an edit counter
  travels with the save). The guarantees above are unchanged: the loss window
  is still the 2 s max-delay plus the time one write takes.

Measured, sustained typing (a key every 60 ms for 6 s, autosave running):

| Note | Each keystroke, before → after | Worst stall while typing, before → after |
|---|---|---|
| 1 MB | 6 ms → <1 ms | 12 ms → 1 ms |
| 10 MB | 46 ms → 1 ms | 148 ms → 17 ms |
| 50 MB | 233 ms → 8 ms | 845 ms → 134 ms |

The keystroke cost was three whole-note copies per key: the edit check compared
the full text to tell typing from re-highlighting, the accessible name read the
whole note for its first line, and the board re-measured a card that is drawn
at maximum height whatever it says. Now a change that alters the length is an
edit without comparing anything (formatting never does); only a same-length
change on a card not already edited still compares. What is left at 50 MB is
the copy autosave has to hand to the worker.

> **Corrected after independent review, same day.** The first version of this
> was measured faster and lost text four ways, each demonstrated by a probe:
> a same-length edit (typing over a selection) on an already-edited card was
> ignored, so a background save of the older text landed and marked it saved;
> `Autosave::flushNow()` did nothing once its text had been handed to the
> worker, so pasting an image or losing focus mid-save did not wait; the board
> rebuilt its cards from rows older than the screen and threw away their
> unsaved state; and a font change mid-edit rebuilt a long note from a copy the
> keystroke fix had stopped refreshing. Now: any change to an edited card
> counts; `flushNow()` always waits; a rebuild **carries** every unsaved card's
> text and unsaved state over to its new card (`ItemCanvas::carried_`); and
> edit counts are unique app-wide, so a late save can never match a different
> card. Each fix has a test, driven through the real autosave timers with a
> third connection holding the write lock to keep a save in flight, that fails
> when the fix is removed — except the app-wide counter, which carry-over has
> made unreachable by any path found; it is kept as a second guard.

### Image write ordering

```
temp file in blobs/  →  fsync  →  atomic rename to blobs/ab/<hash>.png  →  commit row
```

and on delete, commit the row removal *before* unlinking. A reconciliation sweep
at startup quarantines blobs with no referencing row, and flags rows whose blob
is missing (never silently — the item renders as a broken-image placeholder that
the user can remove).

The sweep runs in two halves (`BlobSweeper`, 2026-10-05). Walking the blob
and thumbnail directories — the part that grows with the store — runs on a
worker and only lists files. **Deciding** runs back on the UI thread, against
the references as they are at that moment, and only about files the walk saw.
It must not decide from a snapshot: content addressing means pasting an image
that is already on disk reuses the file, so an orphan the walk listed can be
referenced again before the walk finishes, and deleting it then loses the
picture. Blobs are written on the UI thread, synchronously, so no write is
half-done while the decision runs. `tests/test_images.cpp` sleeps the UI thread
*without* running its event loop to make that interleaving deterministic, and
fails both a worker that deletes and an owner that decides from a stale set.

---

## 9. Stack

Priorities from v1 §31 were internally conflicting — low memory and minimal
dependencies rule out Electron, while rich clipboard and native integration are
where lightweight options are weakest. **The cuts resolve that tension:** with
the file-clipboard matrix and DnD gone, the requirements are now just text
editing, image display, SQLite, theming, accessibility, and Linux-first.

**Recommendation: C++20 + Qt 6 Widgets, CMake.**

- `QClipboard::image()` / `::text()` — clipboard image paste is a two-line
  problem here, which is the entire remaining platform risk.
- Native on KDE Plasma and Wayland, which is the primary target.
- `QPlainTextEdit` gives real text editing, IME, undo/redo, selection for free —
  the thing immediate-mode GUI toolkits (egui, Slint) get wrong, and the reason
  they are ruled out given accessibility requirements.
- Genuine accessibility support (AT-SPI on Linux), which is a hard requirement.
- Widgets, not QML: this is a document-like UI, and QML buys animation
  infrastructure that a restrained app does not want.
- ~60–80 MB resident, sub-300 ms cold start.

**If you would rather write Python:** PySide6 is a legitimate v1 and the
architecture below ports cleanly. The honest cost is ~1 s startup, a 150 MB+
bundle, and PyInstaller packaging pain — a direct hit to two of your stated
priorities, but not a correctness problem.

Tauri becomes viable now that DnD is cut, but on Linux you would still be on
WebKitGTK: a different engine per platform, and the memory/performance wildcard.

Testing: Catch2 or Qt Test. CI on GitHub Actions, Linux job blocking.

---

## 10. Architecture

```
napkin/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── app/       Application, single-instance guard, XDG paths, settings
│   ├── domain/    Buffer, Item, BufferService, SearchService,
│   │              SweepService, ImageService      ← no QtWidgets dependency
│   ├── data/      Database, Migrations, BufferRepository, ItemRepository, Fts
│   ├── media/     BlobStore, Thumbnailer
│   └── ui/        MainWindow, BufferListView, BufferCard, InlineEditor, SearchBar
├── resources/     icons, io.github.sudomonas.Napkin.{desktop,metainfo.xml}
├── packaging/     Flatpak manifest; arch/stable (napkin) and arch/git (napkin-git)
└── tests/
```

`domain/` may depend on QtCore but never on QtWidgets, so the entire model and
every invariant is testable headless in CI.

**Single-instance enforcement**: two processes on one SQLite file is easy to
forget and painful to debug. Who owns the data directory is decided by a lock
file held for the life of the process (`QLockFile`, which takes a native lock
and recognises a dead owner's lock as stale), checked **before the database is
opened**. The local socket only carries the "raise your window" knock; a second
launch knocks for up to three seconds, and if the owner never answers it says
so in a dialog and exits without touching anything.

> **Corrected, 2026-10-05.** The socket used to *be* the lock. Two launches at
> once could both find nobody listening, and the loser of the race to listen
> deleted the winner's socket and listened in its place: two Napkins on one
> database. A primary slower than 300 ms to answer was taken for dead the same
> way. And a second launch opened the database and ran migrations before it
> ever checked. `tests/test_single_instance.cpp` holds the lock in a separate
> process and SIGKILLs it, to show a crash never locks the next launch out.
>
> Running the real binary against a deep test profile then found that a Unix
> socket path longer than `sockaddr_un` allows (108 bytes on Linux) made
> `listen()` fail, so the primary could never be reached. Such a path now puts
> the socket in `$XDG_RUNTIME_DIR` (0700, per user), named after a hash of the
> data directory.

### Data locations

```
Linux    data     ~/.local/share/napkin/napkin/      (XDG)
         settings ~/.config/napkin/napkin.conf
Windows  data     %LOCALAPPDATA%\napkin\napkin\      (AppLocalDataLocation; not yet observed)
         settings registry, HKCU\Software\napkin\napkin
macOS    data     ~/Library/Application Support/napkin/napkin/   (not yet observed)
```

Never beside the executable. Never requires root.

> **Corrected.** This table used to say `~/.local/share/napkin/` and
> `%LOCALAPPDATA%\Napkin\`. Neither was ever true once settings arrived: Qt
> builds `AppDataLocation` from the organisation *and* the application name,
> and `setOrganizationName("napkin")` — needed so a bare `QSettings()` has
> somewhere to write — doubles the folder. Data written before that commit
> (5376a74) was left at the old path and silently stopped being read.
>
> The doubled path is kept deliberately: it is conventional for Qt, nobody
> types it, and moving a user's only copy of their data to tidy a path is a
> migration with real failure modes and no benefit. The Windows and macOS rows
> are what Qt documents for `AppDataLocation`, not observed. On Windows that is
> the *roaming* profile, which is a questionable home for a live SQLite file —
> Phase 9 should decide between it and `AppLocalDataLocation` before anyone has
> data there.
>
> **Decided (Phase 9):** `AppLocalDataLocation`. It is the same directory as
> `AppDataLocation` on Linux and macOS, so nothing moves there; on Windows it
> keeps the database out of the roaming profile. Decided before the first
> Windows build shipped, so no data exists at the other path.

---

## 11. Privacy

Unchanged in spirit from v1 §32/§33, plus three gaps that section missed:

- Data dir `0700`, DB `0600`. On Windows, a protected DACL on the data
  directory — the user, SYSTEM and Administrators — inherited by everything in
  it (`paths::restrictToOwner`).
- Thumbnails live in Napkin's own data dir, never a shared XDG cache that other
  applications and indexers read.
- **Exclude the data directory from desktop search indexing** (Baloo on KDE,
  Tracker on GNOME). Otherwise everything the user "threw away" becomes
  system-wide searchable — a real leak for an app explicitly designed to hold
  half-considered material.

And stated plainly in the README, so nobody misreads the feature name:
**Keep is retention, not encryption. There is no at-rest encryption.**

No account, no cloud, no telemetry, no analytics, no AI, no external API, no
automatic URL fetching. Invariant 4 says there is no socket-opening code path at
all — that is auditable, unlike a policy.

---

## 12. Performance targets

| Metric | Target |
|---|---|
| Cold start to interactive | < 300 ms |
| `Ctrl+N` to caret ready | < 50 ms |
| Search keystroke to results, 5000 buffers | < 100 ms |
| Idle memory, 5000 buffers | < 120 MB |
| Idle CPU | 0% |

Requires: list virtualization, windowed queries (never `SELECT *` over all
buffers), pre-generated fixed-size thumbnails, lazy full-resolution image
loading. **All of these are Phase 2 architecture, not Phase 10 polish.**

### Measured, 500 images + 500 text blocks

`tools/seed_corpus.cpp` fills a throwaway profile; `tools/bench_load.cpp`
measures the real widget tree against it. 1000 items, 500 of them images
(41 MB of blobs: PNG, JPEG, WebP, SVG, 27 animated GIFs).

| | Realistic shape (238 buffers, largest 120 items) | Pathological (1 buffer, 1000 items) |
|---|---|---|
| Cold start to interactive | **235 ms** | **913 ms** — over |
| Search keystroke to results | **3–4 ms** | 3–5 ms |
| Open a buffer (mean / worst of 40) | 60 / 197 ms | 30 / 30 ms |
| Scroll the whole buffer list, thumbs cold | 1388 ms (5 ms/row) | n/a |
| … thumbs warm | 211 ms (<1 ms/row) | n/a |
| Scroll the board, first pass | 706 ms | **20 493 ms** |
| … second and third pass | 665 / 647 ms | 3938 / 3905 ms |
| Live memory (after `malloc_trim`) | **38 MB** | **53 MB** |
| RSS as the OS reports it | 208 MB | 1035 MB |

**Two things this measurement got wrong before it got them right**, both worth
recording because either would have been published as a finding:

1. The seeder reconstructed the profile path as `<XDG_DATA_HOME>/napkin`.
   `AppDataLocation` is `<XDG_DATA_HOME>/<organization>/<application>`, so the
   corpus landed one directory above where the benchmark then looked, and the
   first run reported every target met against an empty database. The tool now
   resolves the path through `paths::` — the application's own code — rather
   than restating the rule.
2. The first memory figure was 688 MB. `QCoreApplication::processEvents()` does
   not dispatch `DeferredDelete`, so a harness built on it accumulates every
   widget the canvas retired and blames the application. Draining them
   explicitly moved the same measurement to 192 MB.

**RSS is not the footprint.** Clearing `QPixmapCache` frees 6 MB; `malloc_trim`
frees the other ~170 MB (~980 MB in the pathological case). That memory is
glibc arena retention from decoding hundreds of multi-megapixel images, not
live data — the application holds 38–53 MB. It is still what a system monitor
shows, so it is still worth a periodic trim on idle.

**The board does not scale to a thousand items in one buffer.** Virtualization
works — 12 cards materialized out of 1000 — but two costs are O(n) regardless:
the board measures every item's height up front (332 ms of the 913 ms cold
start), and the first scroll pays 920px thumbnail generation on the UI thread,
about 108 ms per image. That first pass rendered 190 of the 500 images in
20.5 s; touching all of them would be roughly a minute of frozen UI. The
realistic shape never shows this because no single buffer is large enough.

### Measured again, 2026-10-05: nothing heavy on the UI thread

Same corpus, same machine. `bench_load` now also reports the **longest single
freeze** during each scroll — total scroll time stopped meaning "frozen" once
the work moved to workers, because most of it is now waiting.

| | Realistic shape | Pathological (1 buffer, 1000 items) |
|---|---|---|
| Cold start to interactive | 253 ms | **249 ms** (was 913) |
| Board scroll, first pass: longest freeze | 16 ms | **13 ms** (was a 20 s pass) |
| … every preview made, in the background | 0.8 s | 4.3 s |
| List scroll, thumbnails cold: longest freeze | 15 ms | — |
| RSS 2.5 s after scrolling stops | 46 MB | 74 MB (was 1035) |

What changed: board previews and list thumbnails are made on two worker
threads and delivered to cards that already hold a placeholder of the picture's
exact final size (`Thumbnailer::request`); the board is laid out a piece at a
time — only what the viewport needs before the first paint, the rest in 6 ms
idle slices (`BoardLayout::placeThrough/placeFor`); and the heap is trimmed
once preview work goes quiet. Cold start's remaining ~200 ms is the harness's
own settle time, identical for both shapes.

> **Corrected.** The section above attributes "332 ms of the 913 ms cold
> start" to measuring item heights. Timing the pieces showed measuring the
> whole 1000-item board costs ~126 ms; cold start was *two* full measurements
> — one at the width the window was built at and again at the width it was
> shown at — plus every visible image card decoding its 920 px preview on the
> UI thread, 20–140 ms each. Fixing only the measurement would have left most
> of it.

---

## 13. Export

v1 had no export and no backup story. Local-first without an exit is its own
kind of lock-in, and there is no recovery path if the DB corrupts.

- **Export buffer** → a folder: `.txt` per text item, images as files, plus
  `manifest.json` with timestamps, pin/keep flags, and ordering.
- **Export all** → the same, one directory per buffer, under one dated folder.

Roughly a day of work; disproportionate trust returned.

**Built.** `src/media/Exporter.{h,cpp}`, reachable from **File ▸ Export this
buffer…** and **File ▸ Export everything…**. 240 items including 120 images
export in 15 ms.

The rules that make it a real exit rather than a gesture:

- **Ordinary files.** No archive format, no wrapper. A `.txt` holds exactly the
  text; an image is copied byte for byte and never re-encoded, so the `sha256`
  in the manifest stays checkable against the file beside it.
- **Chronological.** `listForBuffer()` sorts newest-first because that is how
  the board reads. A folder of files is read from `001` downwards, so the
  exporter sorts by position — it inherited the board's order at first and
  inverted every buffer on its way out.
- **Nothing is overwritten.** An export into a folder that already holds one
  gets a new name. Napkin does not destroy what it was not asked to.
- **Skipped items are reported.** A missing blob becomes a line in
  `Result::problems`, surfaced in the dialog's detail text. An export that
  quietly passed over an image would be a backup that quietly is not one.
- **User content becomes a filename here, and nowhere else.**
  `Exporter::slug()` keeps letters and digits and drops everything else, which
  handles `..`, path separators, NUL, newlines, control characters and RTL
  overrides with one rule rather than a list of special cases that has to stay
  complete. Names reserved on Windows are refused too — Phase 9 is the plan,
  and a folder exported on Linux must be one that unpacks there.

---

## 14. Accessibility and error handling

### Accessibility

Keyboard navigation for every action. Visible focus states. Screen-reader labels
(AT-SPI on Linux, which Qt provides). Sensible text scaling. Predictable tab
order. Sufficient contrast in both themes.

**Never encode meaning in color alone.** Pinned and kept states carry an icon and
an accessible label, not just a tint.

`tests/test_accessibility.cpp` sweeps the real widget tree rather than naming
widgets one at a time, so a control added later is covered without anyone
remembering to come back: every visible button and line edit must announce
something, every menu must carry a mnemonic, and every list row must have
`Qt::AccessibleTextRole` text that names its pinned and kept state.

**Text scaling is a layout rule, not a font setting.** Several constants were
pixel counts that happened to fit at the default font. At 200% the card's chrome
estimate was smaller than the footer it had to hold, so the content area shrank
and a link chip clipped its own descenders. `tokens::footerHeight()` and
`tokens::cardChromeHeight()` now derive from the font, as do the start page's
two columns, the empty-state text column and `LinkChip::preferredHeight()`. The
board measures cards from those numbers, so a stale one is not a cosmetic
problem — it is a card too short for the thing it was measured to hold.

### Error handling

Errors are stated in plain language, with the user's content accounted for:

```text
Unable to save image.

The buffer text was saved. The image was not added.

Retry     Cancel
```

No raw stack traces in the UI. Technical detail goes to a log file that contains
no user content (invariant 10). **Never silently discard user content** — if a
write fails, say so and keep what is in memory.

---

## 15. Testing

The domain layer has no QtWidgets dependency, so all of this runs headless in CI.

| Area | Cases |
|---|---|
| Buffer | create, update, delete, pin, unpin, keep, release |
| Lifecycle | sweep skips kept; sweep moves to trash; trash purge; undo restores; confirmed delete of kept |
| Invariant | direct SQL `DELETE` of a kept buffer aborts (trigger) |
| Items | text item, image item, multiple items, ordering, removal |
| Persistence | save, reload, restart, migration forward, WAL recovery |
| Search | text, `source_name`, roll-up to buffer, kept and pinned included, ranking |
| Blobs | store, dedupe on identical paste, retrieve, unlink-when-last-reference, orphan GC, missing-blob handling |
| Drafts | draft with no content writes no row; draft with content writes exactly one |
| Durability | `kill -9` during typing; `kill -9` between blob write and row commit |

Clipboard paste gets an integration test driving `QClipboard` directly — it is
the only remaining platform-dependent surface, so it is the one that must not
regress silently.

---

## 16. Phases

Risk is front-loaded. v1 sequenced the two most platform-dependent features at
Phases 4 and 6, after five phases of UI had been built on the assumption they
would work — and put packaging at the very end, which is a cliff rather than a
phase.

**Phase 0 — Spike. ✅ COMPLETE.** Measured on Arch Linux, KDE Plasma /
Wayland, Qt 6.11.2, GCC 16.2.1, SQLite 3.53.4:

| Risk | Result |
|---|---|
| Clipboard image paste | **Pass.** `image/png` arrives direct and decodes; no transcode needed. |
| Clipboard text paste | **Pass.** `text/plain` alongside legacy `STRING`/`UTF8_STRING`. |
| Format preference order (§4) | **Pass.** `image/png` present ⇒ image wins, as specified. |
| Committed row survives `kill -9` | **Pass**, with WAL + `synchronous=NORMAL`. |
| `journal_mode=wal` persists across restart | **Pass.** |
| Data dir `0700`, DB `0600` | **Pass.** |
| Qt 6 + sqlite3 + CMake + Ninja toolchain | **Pass**, clean build, no warnings. |
| Wayland clipboard needs window focus | **Constraint found** — see §17. |

All three Phase 0 risks are retired. Phase 1 may proceed on this stack.

**Phase 1 — Foundation. ✅ COMPLETE.** Schema v1 with the `guard_kept_delete`
trigger, forward-only migrations on `user_version`, buffer and item
repositories, `BufferService` with draft semantics, single-instance guard, XDG
paths, and 39 test functions across 5 binaries — all passing.

Enforced structurally rather than by convention:

- `napkin_core` links `Qt6::Core` but **not** `Qt6::Widgets`, so the
  domain-has-no-GUI rule is a link error rather than a code-review note.
- The delete guard needs `PRAGMA recursive_triggers=ON`: without it,
  `INSERT OR REPLACE` deletes the replaced row **without firing the trigger**,
  which was a hole straight through invariant 1. The guard stops deletion, not
  an `UPDATE` that clears `kept` first — releasing a keep is a legitimate user
  action, so the trigger is an assertion against automatic *deletion*, not a
  capability model.
- `moveToTrash()` **returns false** for a kept buffer. The only way past it is
  `moveToTrashConfirmed()`, so no UI path can forget to ask (§6).
- Confirmed deletion of a kept buffer **releases the keep** as it trashes. No
  legitimate path therefore ever hard-deletes a `kept = 1` row, which leaves the
  trigger as a standing assertion against service-layer bugs.
- Blob liveness is `SELECT 1 FROM items WHERE blob_hash = ?`, not a refcount, so
  no drift is possible.

Verified against the real on-disk database, not just in-memory: an external
`sqlite3` process running `DELETE FROM buffers WHERE kept=1` is refused with
*"refusing to delete a kept buffer"* and the row survives — acceptance
criterion 17, end to end.

*Bug found and fixed during verification:* `napkin.db` and its `-wal`/`-shm`
sidecars were left at `0644` on a first run, because permissions were applied
before SQLite had created the files. `secureDatabaseFiles()` now runs after
`open()` and covers the sidecars, which carry uncommitted user content.
Regression test: `tests/test_paths.cpp`.

**Phase 2 — Text. ✅ COMPLETE.** Virtualized buffer stack with PINNED/RECENT
sections, draft creation, inline expansion editing, two-timer autosave,
relative timestamps, empty state. 19 new test functions (58 total, 8 binaries).

Measured against §12 on a 5000-buffer database, file-backed, not in-memory:

| §12 target | Measured |
|---|---|
| `listLive(5000)` metadata | **3 ms** |
| Previews for a screenful (12 cards) | **<1 ms** |
| Windowed query at offset 4980 | **2 ms** |
| Idle RSS | **78 MB** (target <120 MB) |

The list holds metadata only — roughly 48 bytes a row, so 5000 buffers is a
quarter of a megabyte. `sizeHint` does no text layout.

> **Corrected after review.** An earlier version of this paragraph claimed
> previews were fetched "lazily per visible row" and that `sizeHint` was O(1).
> Neither was true: `data()` computed the preview *before* the role switch, so
> every metadata read — including the `IsExpandedRole` that `sizeHint` reads for
> **every** row — issued two queries and loaded each buffer's text. A
> 5000-buffer startup executed ~10,000 statements. Metadata roles now return
> before any preview is touched, the preview cache is bounded, and preview text
> is truncated in SQL (`substr(text, 1, 2048)`) and again at 256 characters, so
> pasting a minified bundle no longer makes `elidedText` O(text length) on every
> repaint.

Behaviours that only exist once the UI is wired up, so they are covered by
headless GUI tests driving the real widget tree (`tests/test_editing.cpp`):

- `Ctrl+N` shows a card but writes **no row** until there is content (invariant 5).
- An abandoned empty draft evaporates — it never existed to clean up.
- `Esc` and window-close both flush *before* collapsing, so the debounce window
  is never a data-loss window.
- **The list does not re-sort while a card is expanded.** Autosave bumps
  `modified_at` continuously, so a naive reload would yank the card you are
  typing into to the top. Reloads are deferred and applied on collapse.
- **A paste into an existing napkin moves it to the top of RECENT at once.**
  A paste is a discrete act, not typing, so the freeze above does not apply.
  *Corrected 2026-10-06:* pasting (Ctrl+V, a drop, or the global shortcut)
  refreshed the card's time but never re-sorted, so the napkin stayed where it
  was — even under OLDER, claiming "just now" — until some unrelated change
  reloaded the list.

*Design change during implementation:* `Ctrl+N` is a `QAction`, not a bare
`QShortcut`. A headless window is never active, so key-chord delivery cannot be
relied on in tests — and the spec wants shortcuts discoverable anyway, which an
action carrying its own label and key hint gives for free.

**Phase 3 — Pin, Keep, Trash. ✅ COMPLETE.** Both flags with drawn indicators,
list-scope keys, context menu, soft delete, undo toast, trash view and the
confirmation flow. 12 new GUI test functions (70 total, 9 binaries).

- **Pin moves the card, Keep does not.** Pinning reloads the list — that is the
  point of it. Keeping refreshes one row in place, so the stack never reshuffles
  for a lifecycle change. Both are asserted.
- **The confirmation cannot be skipped.** `trashRow()` calls `service.trash()`,
  which *returns false* for a kept buffer; the dialog is the only route to
  `trashConfirmed()`. A UI path that forgot to ask would simply fail to delete.
- **Every delete is soft and undoable** for 8 seconds, from a toast, without
  hunting for the trash. A second delete replaces the standing offer — the most
  recent is the one the user most likely meant.
- Pin and Keep are inert in the trash view; `Delete` restores there instead.
- **Empty trash** is available while viewing the trash, behind a confirmation
  that names how many buffers it will destroy.
- Indicators are **drawn vector glyphs**, not an icon theme: a pushpin and a
  bookmark. Deliberately not a padlock — Keep is retention, not security, and
  the icon must not promise otherwise. Each state is also carried in the row's
  accessible label, so nothing is encoded in styling alone.

The header now carries the app name and the trash toggle, with the gap between
them reserved for the Phase 5 search field.

**Phase 4 — Images. ✅ COMPLETE.** Clipboard paste, content-addressed blob
store, thumbnailer, card thumbnails, lightbox, image picker and the
reconciliation sweep. 21 new test functions (91 total, 10 binaries).

- **The §4 preference order is a tested contract.** A `QMimeData` carrying both
  `image/png` and a URL — exactly what a browser's *Copy Image* produces —
  resolves to the image. Paste is intercepted in `QPlainTextEdit` itself, which
  would otherwise drop the image and paste the URL alongside it.
- **Invariant 6 end to end:** the blob is written to a temp file, fsynced, the
  containing directory fsynced, atomically renamed, and only then does the row
  commit. A crash leaves an orphan blob, never a dangling reference.
- **Invariant 7:** rows commit before any unlink, and `reconcileBlobs()` at
  startup reclaims orphans, deletes `.tmp` files from interrupted writes, and
  *reports* rows whose blob has vanished rather than rendering them silently
  blank — a missing image draws a visible placeholder.
- Thumbnails are scaled during decode, so a 4000×3000 photo never lands in
  memory whole, and are cached on disk plus in `QPixmapCache`.
- Migration v1 → v2 verified against a real v1 database: rows preserved, image
  rows backfilled to `image/png`.

Storing an image into an open empty draft promotes it in place rather than
creating a second buffer — the draft invariant survives contact with images.

**Phase 5 — Search. ✅ COMPLETE.** FTS5 with buffer-level roll-up, a persistent
header field, ranked results and a snippet showing why each one matched. 18 new
test functions.

- **Schema v5** adds `items_fts`, an *external-content* table: FTS5 keeps only
  the index and reads the columns back from `items`, so a pasted log is not
  stored twice. Triggers maintain it, which is the price of external content —
  such an index does not maintain itself. A just-pasted item is searchable
  immediately; there is no moment where new content is invisible.
- **`source_name` is indexed beside the text**, because looking for a filename is
  the same act as looking for a word and nobody remembers which column their
  memory lives in.
- **Results roll up to the buffer**, since that is what the list shows: a hit on
  item 3's filename surfaces the whole buffer.
- **What the user typed is not a query language.** Every token is quoted, so
  `AND`, `OR`, `NOT`, `NEAR`, an apostrophe or a stray quote are searched for
  rather than executed or rejected. The final token gets a prefix wildcard, so
  results narrow while a word is still being typed.
- **Trashed buffers are excluded**; pinned and kept ones are not. Search is for
  finding what you have.

**Search narrows both panes.** The list shows the buffers that matched; the
board then shows only the *items* that matched, with the term marked inside the
card text by a `QSyntaxHighlighter`. Finding which buffer matched and then having
to re-read it hunting for the word is half an answer.

A banner says what is hidden — *"2 of 4 items match ‘nginx’"* — with **Show all**
beside it, because the surrounding items are often the context you actually
wanted, and a board that silently hid two thirds of a buffer would look like the
buffer had lost them. Changing or clearing the query keeps you on the buffer you
are looking at rather than throwing you to the top of the restored list.

**5 ms across 2000 buffers**, so it runs on every keystroke behind a 120 ms
debounce that only exists to stop a fast typist re-querying mid-word.

> `LIMIT -1` inside the ranking query's CTE is load-bearing. FTS5's `bm25()`
> and `snippet()` only work when the index is the direct subject of the query,
> and SQLite flattens an ordinary CTE into the outer join — which puts them
> back somewhere they refuse with *"unable to use function bm25 in the
> requested context"*. A subquery with a LIMIT is never flattened into a join.
>
> **Corrected.** This note used to credit `AS MATERIALIZED` with that job. It
> does it only from SQLite 3.39: on 3.35–3.38 the hint does not stop the
> flattening, and before 3.35 it is a syntax error. It passed here on 3.53 and
> in CI on 3.45, and failed every search with a hit on Ubuntu 22.04's 3.37.2,
> which is where the release is built. Bisected against the 3.38.5 and 3.39.4
> amalgamations. The suite now passes against 3.31.1 and 3.37.2 as well, and
> CI runs it against both.

**Phase 6 — Sweep. ✅ COMPLETE.** The `OLDER` section, the nudge, the review
dialog and sweep-to-trash. 14 new test functions.

§6 has described this lifecycle since v2 and nothing implemented it:
`olderThanCutoff()` was dead code, there were only PINNED and RECENT sections,
and there was no sweep at all. The spec described an app that did not exist.

- **`OLDER`** is a third section below RECENT, drawn a touch quieter. Age changes
  where a buffer sits, never whether it exists — a year-old buffer is still
  there, still searchable, still one click away.
- **The nudge** is one quiet line above the list, and only when at least 12
  buffers are past the cutoff. Napkin tolerates accumulation; the offer should
  feel like a convenience, not a scolding. It can be waved away for the session.
- **The review dialog** shows exactly what it proposes to take, everything
  ticked, and says what it is leaving alone and why. Untick anything you want to
  keep.
- **A sweep trashes, never deletes.** It goes through the ordinary undo toast,
  and the whole sweep is undoable as one action.

> **Placement and lifecycle are different questions, and the code now says so.**
> `isOlder()` excludes pinned buffers — a pinned buffer is not in the recency
> order at all, so it cannot be "older" within it. `sweepableCount()` does *not*
> reuse that: only **Keep** exempts a buffer from a sweep. Conflating them would
> have made pinning a silent second Keep, which is exactly the confusion §3
> exists to prevent. A test asserts a pinned, un-kept, old buffer is offered.

**Phase 7 — Refinement. ✅ COMPLETE.** Menu bar and settings; the three empty
screens; complete-palette theming; link chips (§3, promised since the first
draft and delivered nowhere until now); export (§13); and the accessibility
pass (§14), which found two unlabelled controls and turned text scaling from a
font setting into a layout rule.

The menu bar is **File / Home / Trash / Settings** and carries every action the
application has, because it is the one place a user can go to find out what the
app can do — the header buttons and the key chords are shortcuts *to* these, not
a separate set. **Home → All buffers** is one gesture back to the ordinary view
from wherever you are: out of the trash, out of a search, back to the top.

Settings is deliberately small, in two groups: **Appearance** and **Lifecycle**.
Napkin's premise is that you do not configure it, you throw things at it — so
nothing here is a knob for its own sake. The lifecycle values are read through
`BufferService` from `QSettings`, so the domain layer picks them up without
depending on any dialog.

Appearance holds theme, accent, typeface and text size. That is not fiddling:
it is legibility. Someone who needs a larger face or a different typeface to
read comfortably is not configuring the app, they are making it usable at all
(§14). A live preview shows the chosen face and size, because a font named in a
drop-down is a font you have to imagine.

- **Accent** is a short named set with swatches, not a colour wheel — Napkin is
  not a theming engine. It applies under *every* theme including "Follow the
  system", since the accent is the one part of the platform theme worth
  inheriting and therefore the one worth being able to override.
- **The paired text colour is measured, not guessed.** Choosing it by
  `QColor::lightness()` put white on a mid green at **2.9:1**: HSL lightness is
  not luminance — green carries most of the visible energy and blue almost none,
  which a lightness value does not know. `tokens::textOn()` computes both
  candidates and takes the better. All six accents now clear 3:1 with margin.
- **Text size scales from the platform's font, never from the current one.**
  Scaling the already-scaled font is the obvious way to write it and grows
  without bound every time the dialog is saved.
- **A font change invalidates the board.** `BoardLayout`'s measurement cache is
  keyed on the item, its last edit and the column width — the font is not in
  that key, so `setFont()` clears the cache. Without it every card kept the
  height its old face needed: *117px at 1x and 117px at 2x*. Widgets that derive
  metrics from the font at construction (the card footer, the link chip, the
  start page's columns, the empty-state column) re-derive them on
  `ApplicationFontChange`.

**Phase 8 — Linux delivery.** In progress.

**CI ✅.** `.github/workflows/ci.yml`. Linux is the gate: configure, build, the
whole suite, then install and validate what a desktop actually reads — the
`.desktop` entry with `desktop-file-validate` and the metainfo with
`appstreamcli`. Ubuntu 24.04 ships Qt 6.4 and this needs 6.5, so Qt is installed
explicitly; that also means Linux and Windows build against the same Qt, and a
failure is about the platform rather than the toolchain. Every step was
rehearsed locally before being written down.

A **Windows job runs informationally**, allowed to fail. SPEC §16 puts Windows
after Linux is genuinely good, and this job exists to replace guesses about the
port with a build log. What is already known to need work: `BlobStore` uses
`::fsync` and `::open(O_DIRECTORY)` and Windows has no directory sync at all, so
invariant 6's durability argument has to be re-derived rather than translated;
`QFile::rename` onto an existing path fails on Windows, which the
content-addressed blob store does whenever the same image is pasted twice; and
`QFile::setPermissions` maps to the read-only flag rather than an ACL, so §11's
0700/0600 promise would be quietly untrue there.

**Phase 9 outcome — Windows builds and passes all 21 suites (2026-09-19).**
What each of those turned out to need:

- *Durability:* `_commit` in place of `fsync`, and `MoveFileExW` with
  `MOVEFILE_WRITE_THROUGH` in place of rename-then-sync-the-directory; the
  write-through move is Windows' own answer to "the rename is on disk".
- *Rename onto an existing blob:* `QFile::rename` refuses that on Linux too, not
  only Windows. Because blobs are named by their hash, losing that race means
  the identical file is already there, and it is now treated as the dedupe it
  is.
- *Privacy:* see §11. **Corrected:** the commit that added the Windows DACL
  (6976555) said the first Windows run "showed the data directory's ACL
  granting group and world access". It did not. It showed Qt's *permission bits*
  as `0x7777`, and a break-test — the same suite with the DACL disabled — still
  passed, because a stock profile folder is already owner-only. The `0x7777`
  is how Qt maps an ACL onto Unix bits, not exposure. The protected DACL is
  kept because it holds when the parent folder has been loosened, and
  `test_paths` now loosens the parent (an inheritable Everyone-read entry)
  before checking, so it tests that case rather than the stock one.
- *Single instance:* a named pipe keyed on a hash of the data directory.
- *Found only by running there:* test binaries were GUI-subsystem executables
  with no stdout, so the first failures arrived with no output; MSVC read
  sources in the system code page until `/utf-8`; and export wrote notes with
  `QIODevice::Text`, which turns `\n` into `\r\n` on Windows, so the exported
  file was not the text that had been typed.

**One identity ✅.** The application id is `io.github.sudomonas.Napkin`, and the
`.desktop` file, every icon and the metainfo are named after it. AppStream and
Flathub treat the id, the desktop basename and the icon name as one thing; a
mismatch means the listing and the launcher are unrelated objects. It is
`io.github.*` rather than `org.napkin.*` because an AppStream id is a claim to a
domain and Flathub checks it — naming it after a domain nobody here controls
would be a claim that is simply untrue.

**Flatpak manifest written, not yet built.** `packaging/io.github.sudomonas.Napkin.yml`.
No `--share=network` and no `--filesystem=host`: §11 says everything stays on
this machine, and a sandbox that granted either would make that a matter of
trust rather than of fact. File chooser and export go through the portal, which
hands back exactly the file the user picked. `flatpak-builder` is not installed
here, so **the manifest is unverified** — it is a starting point, not a
delivered artefact.

**Tray mode ✅.** Off by default — Napkin is somewhere to throw things, not a
resident service — but it can only catch what is thrown at it if it is already
running. `src/ui/TrayIcon.{h,cpp}`, with a setting under Lifecycle.

The rule that matters here is the one about not stranding anyone: whether a tray
exists is a property of the desktop, not of Napkin, and closing the only window
to an icon that never appeared would leave the application unreachable with the
user's data inside it. So `keepInTray()` answers false on a session with no
tray *whatever is stored*, and `closeEvent` additionally requires the icon to be
actually showing before it hides instead of quits. `tests/test_tray.cpp` pins
both; removing the availability check turns the refusal test red.

**AppImage and the release process ✅.** `.github/workflows/release.yml`, driven
by a `v*` tag. It is the same build CI already runs plus the artefacts, on
purpose: a release path that differs from the tested path is a release path
nobody has tested. The suite runs before anything is packaged, and the bundle is
smoke-tested by running `--version` out of it and checking the version matches
the tag — so a release cannot ship a binary built from somewhere else. `--help`
and `--version` are answered before the single-instance check and before the
database is opened, verified to create no files.

Its first run, for v0.1.0, **failed at the test step and published nothing** —
which is the ordering doing its job. The ubuntu-22.04 runner's SQLite 3.37.2
broke every search with a hit (§20), something CI on 24.04 could not see. The
tag has to be re-cut once the fix is in.

**The global capture hotkey is NOT done.** §17 calls it the highest-leverage
single addition and it remains so, but it is not built, and the reasons are
worth recording rather than leaving as an empty box:

- The portal is there: `org.freedesktop.portal.GlobalShortcuts` **version 2** is
  present on KDE/Wayland, and `CreateSession` returns a Request path. Both
  verified directly against the session bus.
- What is not verified is everything after that. The session handle arrives on a
  `Response` signal against a Request object, `BindShortcuts` is a second
  round trip that shows the user a prompt, and the key combination is chosen by
  the desktop rather than by us. None of the activation path can be exercised
  headless, because it ends in a human pressing a key.
- An attempt at it derived the session path from the token "by convention",
  which is guesswork, and a probe written to check that guess crashed. Shipping
  ~250 lines of D-Bus that cannot be tested here would be exactly the kind of
  unverified claim this document exists to prevent.

What it needs: a session with a real desktop to develop against, the portal
handshake written against the `Response` signals rather than derived paths, and
the Phase 0 constraint honoured — Wayland serves the clipboard only to a focused
client, so the capture window must appear and take focus *before* the clipboard
is read. Roughly a day with a desktop to test on; not doable blind.

**Phase 9 — Windows and macOS.** Only after Linux is genuinely good.

Packaging was claimed as Phase 0 work and was in fact never started. It is now
**partly** done, and the rest is honestly outstanding.

**Done:** the application has an identity. `resources/napkin-source.png` is the
artwork; the hicolor set (16 → 512) is generated from it and committed, so there
is no build-time image dependency.

> **New artwork, 2026-09-19:** a crumpled napkin replaced the paper-clipped
> square. The source is kept exactly as supplied. Generating the set: crop to
> the opaque content (x 52–426, y 77–446), centre it on a square with 18% room,
> lift it by 1/40 of the side, then draw a shadow under it — its own alpha,
> box-blurred three times at radius side/45, dropped side/30, near-black at
> 42%. Without the shadow a white napkin all but vanished at 16–22 px on a
> light panel. The content is ~375px across, so the 512px icon is slightly
> upscaled; a larger original would make it crisper.

`resources/io.github.sudomonas.Napkin.desktop` passes `desktop-file-validate`.
`install()` puts the binary, the desktop entry and the icons where XDG expects
them, verified by installing to a scratch prefix. The icons are also compiled
into the binary, so a build run straight out of the source tree still has a
window and taskbar icon — `setDesktopFileName()` previously promised the
compositor a file that existed nowhere, and the name it passes, the `.desktop`
basename and the app id must stay identical or Wayland gives the window no
taskbar identity.

> **Corrected, 2026-09-20.** This section said *"no CI, no Flatpak or AppImage,
> no `.desktop` MIME association, and no release process"* long after three of
> those four had shipped. A status list that is not re-read is worse than none:
> it is the document asserting something false about itself.

**Done since:**

| | |
|---|---|
| CI | `.github/workflows/ci.yml` — Linux (`ubuntu-24.04`) and Windows (`windows-2022`) on every push |
| Release process | `.github/workflows/release.yml` on a tag push: `appimage`, `windows`, `publish`. Five releases, v0.1.0 → v0.1.4 |
| AppImage | built and published; `ubuntu-22.04` deliberately, being the oldest glibc, so the bundle runs on more |
| Windows | `napkin-vX.Y.Z-windows-x64.zip` published alongside |
| Arch | `packaging/arch/stable` (`napkin`, from the release tarball) and `packaging/arch/git` (`napkin-git`, from master). The suffix is an Arch requirement for a VCS package, and dropping it is what let a stale `/usr/bin/napkin` shadow newer local builds |

**Still outstanding, and accurate as of 2026-09-20:**

- **No `.desktop` MIME association.** The entry has no `MimeType=` line, so
  Napkin is not offered as a handler for anything.
- **The Flatpak is written but never built.** `packaging/io.github.sudomonas.Napkin.yml`
  exists and is deliberately scoped — no `--share=network`, no filesystem access
  beyond the app's own data (§11) — but no workflow builds it and it is not on
  Flathub. A manifest nothing exercises is a claim, not a package.

---

## 17. Deferred, with intent

Explicitly *not* in v1, but the design does not foreclose them:

**Global capture hotkey.** The real friction is not `Ctrl+N` — it is
alt-tabbing to Napkin at all. A system-wide hotkey opening a small capture
window would be the highest-leverage single addition, and it is Phase 8 rather
than Phase 1 for two reasons: on Wayland it requires the
`org.freedesktop.portal.GlobalShortcuts` portal, whose support is uneven, and
because of the focus constraint measured in Phase 0 (below).

> **Phase 0 finding — Wayland serves the clipboard only to a focused client.**
> A Qt client with no activated window reads an *empty* format list; the same
> client with a shown, activated window reads `image/png` fine. This is a
> Wayland security property, not a Qt bug.
>
> Consequence: a background hotkey handler **cannot** silently read the
> clipboard. The capture window must actually appear and take focus before
> reading. That is acceptable — it is the interaction we want anyway — but it
> rules out a "capture invisibly on hotkey" design, so do not plan for one.
>
> **This constraint was then broken by the one path that already had it.** The
> tray's "Paste onto a new napkin" raised the window and read the clipboard in
> the same call stack — but `activateWindow()` is a *request* to the
> compositor, not a synchronous change, so the read happened before any focus
> arrived and came back empty. The paste returned, having already made the
> napkin, so the user got a blank napkin and no explanation. Anything that
> reads the clipboard after a raise now goes through
> `MainWindow::whenWindowIsActive()`, which polls for focus and gives up out
> loud after a second rather than losing the paste in silence.

**Drag and drop.** The item model is type-tagged and position-ordered
specifically so a drop handler is additive: it constructs the same items paste
does. Re-add when Linux feels finished.

**A read mode.** Opening a buffer currently means editing it; there is no way to
simply look at a long one. Acceptable for a scratchpad, and surfaced only by
reviewing the master-detail mockup in §7, but worth revisiting once the content
types are richer.

**Generic files.** Deliberately closed. Reopening it means reopening large-file
policy, MIME handling, and the file-manager identity question. Do not.

---

## 18. Acceptance criteria

The happy path, from v1 and still correct:

1. Launch, `Ctrl+N`, type immediately.
2. Paste a screenshot into the same buffer.
3. Paste a URL; it renders as a link chip.
4. Close the application. Reopen. The buffer is there.
5. Pin it. Keep it. Create a dozen throwaway buffers.
6. Search across them.
7. Sweep. Verify the kept buffer is untouched.
8. Undo the sweep. Verify everything comes back.
9. Never once created a title, folder, category, tag, notebook, or account.

And the adversarial path, which v1 omitted entirely:

10. `kill -9` mid-typing → content survives to the last flush.
11. `kill -9` mid-image-paste → no dangling reference; at worst an orphan blob, GC'd on restart.
12. Disk full during a blob write → clear error, buffer intact, nothing half-written.
13. Read-only data directory at startup → refuses to start with an actionable message, never a stack trace.
14. Blob deleted out from under the app → broken-image placeholder, removable, no crash.
15. Second instance launched → existing window raises.
16. 5000 buffers → scroll and search stay inside §12 targets.
17. Direct `DELETE FROM buffers WHERE kept=1` via sqlite3 → **aborts.**

---

## 19. Two crashes from real use, and what they had in common

Both came from the same root: **`editingBuffer_` kept naming a row after that row
stopped being live.** A buffer trashed from the list, or purged by *Empty trash*,
left the canvas still pointing at it.

| Symptom | Cause |
|---|---|
| Pasting after deleting the buffer **crashed the application** | Appending to a purged row violates the foreign key. `DbError` unwound into Qt's event loop, which calls `std::terminate`. |
| A buffer stayed in the list with no items, still showing its old text | Milder form of the same thing: the paste landed *inside* the trashed buffer, so the text accumulated somewhere invisible. |

Two rules now hold, and both are tested:

1. **`currentBufferIsLive()` is checked before any write.** If the row has gone,
   the canvas lets go of it and the next capture starts a new buffer.
2. **No exception may reach the event loop.** `napkin::Application` overrides
   `QCoreApplication::notify()` and catches everything, because every event in a
   Qt program passes through there.

> **An earlier version of this section claimed "every database-writing slot runs
> through `guarded()`". That was false**, and a later audit reproduced two
> `SIGABRT`s to prove it — one of them on the *Delete key*. Wrapping individual
> write calls could never have been enough: a failing **read** during a row
> click, a query issued from inside `paint()`, and `MainWindow`'s own
> construction were all outside every wrapper. The boundary has to be under
> everything, not sprinkled over the paths someone remembered.

> The lesson generalises past these two: a `DbError` escaping a Qt slot is always
> a crash, never an error dialog. The type has existed since Phase 1 and the
> boundary that catches it did not.

---

## 20. Review findings and corrections

An independent adversarial review (security/performance and UI/UX, run as two
separate agents with an explicit brief to find what is wrong and to treat this
document as claims to verify rather than facts) produced the corrections above
and the fixes below. Recording it here because **several claims in earlier
versions of this spec were false, and one test had been written so that it could
not fail.**

| Finding | Severity | State |
|---|---|---|
| SVG could read local files via a bare `href` path | high, privacy | fixed — refused at import; §4 corrected |
| Text silently lost when a save fails, and on close | **critical** | fixed — collapse and close now refuse, and say so |
| Failed saves retried ~3×/second, for ever, in silence | medium | fixed — bounded retry, then a message |
| Undo of a confirmed delete silently dropped the Keep flag | high | fixed — undo restores `kept` and `modified_at` |
| Thumbnails were never deleted; `forget()` was dead code | high, privacy | fixed — the sweep now reclaims thumbnails too |
| `INSERT OR REPLACE` bypassed the kept-delete trigger | high | fixed — `PRAGMA recursive_triggers=ON` |
| Previews computed for every row, not per visible row | high, perf | fixed — metadata roles touch no preview |
| Preview cache unbounded; 253 MB at 5000 rich buffers | high, perf | fixed — bounded, and text truncated |
| `paint()` was O(text length): 153 ms for a 1 M-char line | high, perf | fixed — truncated in SQL and again at 256 chars |
| A 48 KB PNG declaring 20000×20000 was accepted | medium | fixed — 80-megapixel ceiling |
| Failed thumbnails re-decoded on every repaint | medium | fixed — negative results cached |
| Instance socket was world-connectable in `/tmp` | high | fixed — 0700 data dir, `UserAccessOption` |
| Empty-trash dialog counted rows it would not delete | low | fixed — counts what will actually go |
| `Ctrl+N` in the trash created a live buffer shown in the bin | medium | fixed — returns to the live list first |
| `Delete` in the trash *restored* instead of deleting | medium | fixed — Delete destroys (confirmed), `R` restores |
| Timestamps at 2.71:1 contrast; four styles failed WCAG AA | high, a11y | fixed — alphas raised to measured thresholds |
| Hover state was a 1.04:1 change, i.e. invisible | medium | fixed |
| Pin glyph read as a magnifying glass | medium | fixed — redrawn with crossbar and point |
| Three `QAction`s attached to no menu — bindings undiscoverable | high | fixed — ＋New button, ⋯ menu, shortcut sheet |
| Cards stretched to full window width | medium | fixed — 760px measure, centred |
| No accessible names; no initial selection for keyboard users | high, a11y | fixed |
| Test counts in this document were inflated | — | corrected (`PASS` lines counted init/cleanup) |
| Every search with a hit threw on SQLite < 3.39 (Ubuntu 22.04, the release host); v0.1.0's release run failed on it | **critical** | fixed — `LIMIT -1` replaces `AS MATERIALIZED`; CI tests SQLite 3.31.1 and 3.37.2 |
| A query that threw inside `beginResetModel()` left the list model mid-reset, rows cleared, query already changed | high | fixed — query first, reset second; query and mode roll back on failure |
| *Usability test, 2026-09-19:* a deleted item was gone for good once the undo toast expired | **critical**, data | fixed — items go to the trash as a napkin (§6) |
| *Usability test:* typing on an empty napkin or the start page vanished; with the list focused, letters were list commands (P pinned, K kept) | high | fixed — typing starts a note from all three places; criterion 1 of §18 now holds |
| *Usability test:* Napkin set to Dark on a light KDE desktop had invisible menu-bar labels (and Light on a dark desktop a black bar) — Breeze paints the bar in the desktop scheme's header colours, not the app palette | high, a11y | fixed — the bar is styled from Napkin's palette and restyled on every palette change |
| *Usability test:* after Clear search a napkin was highlighted with "Select a napkin" beside it, and clicking it did nothing — the board only followed *changes* of the current row | medium | fixed — the paths that blank the board re-open or clear the row, and a click always opens what it lands on |
| *Usability test:* "Empty trash" stayed on the ordinary list after emptying from the menu (three of four visibility checks ignored the mode) | low | fixed — one check, used everywhere |
| *Usability test:* the Settings dialog opened squeezed, clipping the preview and the Lifecycle notes; the preview ignored the chosen theme | medium | fixed — sized to its content; the preview shows the theme before Save |
| *Usability test:* no right-click menu on items; double-clicking a word while editing placed a caret instead of selecting it | medium | fixed — Edit/Open, Copy, Cut, Delete on right-click; native word selection while editing, and the word is selected when a double-click starts editing |
| *Usability test:* list times went stale after an edit, and card times never updated at all | low | fixed — a changed napkin re-reads its row in place; cards tick with the list |
| *Usability test:* Tab reached the list and the board with no visible focus | medium, a11y | fixed — the dotted accent ring marks the focused row and card |
| *Usability test:* Escape on an empty new note left a blank card; an empty napkin's board kept the previous napkin's items | low | fixed — the unwritten note is discarded; the board clears everything |
| *Usability test:* "Clean up" with nothing to clean opened an empty list with "Move to trash" as default; "(s)" printed literally in counts (no translation is loaded) | low | fixed — a message instead; the button needs a ticked row; plurals spelled out |
| *Usability test:* a link's host was cut mid-letter ("www.example.c"); dark pictures vanished into dark cards | cosmetic | fixed — host elided by whole labels, "www." dropped; images carry a hairline edge |
| *Usability test:* a napkin's list title followed every edit and addition, so it could not be recognised; every unnamed image was titled "Screenshot" (an interpretation §1 rules out) and that word was unsearchable | medium | fixed — titled by what was put on it first; unnamed images are "Image" |
| *Usability test:* one action had three names ("Write a note", "New text block", "New text block on this napkin"); items were "item", "card" and "block"; "Settings ▸ Preferences…" opened "Settings"; Help lived under Settings; Keep was explained only as "never removed by a sweep" | medium | fixed — "note" for text, "New note" everywhere; menus are File (with Settings…), Napkins, Trash, Help; Pin and Keep explained in tooltips; About shows the version |
| *Second usability test:* bare P and K in the list silently pinned and kept — typing "pack" did both — once typing made notes everywhere else | high | fixed — typing in the list writes on the selected napkin; Pin and Keep are Ctrl+P and Ctrl+D, with a toast and Undo |
| *Second test:* undo lived only in an 8-second toast | medium | fixed — Ctrl+Z undoes the last delete, pin or keep; the toast pauses under the pointer |
| *Second test:* a restored item came back as a napkin of its own; Restore was silent and left the napkin on the trash board | medium | fixed — items go back into the napkin they came from if it is still live (schema v6, `restores_to`); Restore says where things went |
| *Second test:* a cut left its original in the trash after the paste had completed the move | low | fixed — discarded once pasted, only when the clipboard still holds what was cut and that is all of it (a mixed selection copies as text only, so its images stay recoverable) |
| *Second test:* a napkin that began with a picture or link was titled "Image" or by its raw URL; pasting in the trash did nothing | low | fixed — titled by its first note, then its first link (short form), then its first image; paste leaves the trash as Ctrl+N does |

| Only the footer button said "Copied"; `Ctrl+C` and right-click ▸ Copy were silent, so the keyboard route to the most-used verb gave no sign it had worked | medium | fixed — `copySelection()` acknowledges, so every route does; several at once say so in the toast |

| The tray's **Quit Napkin** did nothing whenever the window had been closed to the tray | high | fixed — `quitNapkin()`; `close()` ends the process only via `quitOnLastWindowClosed`, which never fires for an already-hidden window (measured both ways) |
| **File ▸ Quit** merely hid the window while the tray setting was on, because `closeEvent` hides to the tray | high | fixed — the same `quitNapkin()`, so both routes out of Napkin mean the same thing |
| The tray's **Paste onto a new napkin** made a blank napkin and pasted nothing | high | fixed — it waits for the window to actually take focus before reading the clipboard (§17), and makes the napkin only once there is something to put on it |

| `Ctrl+A` selected only the virtualized visible band: on a 60-item napkin it selected 13, copied 13, and `Ctrl+A`+`Delete` left 47 items behind silently | **critical** | fixed — selection, cursor and copy are computed over `items_`, not `cards_` (§7) |

| *User, 2026-09-20:* `Down` moved to the card on the right, not the one below — arrows stepped document order, so on three columns they crossed the top row before descending and the card beneath the cursor was unreachable | medium | fixed — `BoardLayout::neighbour()`; Up/Down within the column, Left/Right across (§7) |

| Copying a lone image whose blob was gone put the literal `[image]` on the clipboard and reported "Copied"; **cutting it did the same and deleted the item anyway** | high | fixed — one `putSelectionOnClipboard()` for both verbs; a cut that cannot copy removes nothing (§7) |

| *2026-10-05:* the window swept `paths::thumbsDir()` — the **real profile's** thumbnail cache — so any test that emptied the trash deleted every real thumbnail its own temporary database did not reference | high | fixed — the sweep uses the `Thumbnailer`'s own directory |
| *2026-10-05:* a second launch opened and migrated the database before the single-instance check; two simultaneous launches could both run; a slow primary was taken for dead | high, data | fixed — a lock file decides, before the database is opened (§10) |
| *Independent review, 2026-10-05:* the new lock alone would have started a second Napkin beside a running 0.1.7 or earlier, which holds no lock — and deleted its socket | high, data | fixed — even holding the lock, it knocks on the socket before taking it over |
| *Review:* cut, then delete, then paste: the delete forgot the pending cut, so with stacked offers undoing everything put back a second copy of what was cut | medium | fixed — only a new cut replaces a pending one |
| *Review:* a missing list thumbnail drew whichever GIF frame was playing in another row | low | fixed |
| *Review:* previews 0.1.7 had scaled UP were still served — blurry, and 3 px off the placeholder | low | fixed — a cached preview larger than its source is remade |
| *Review:* the list-thumbnail test claimed a repaint it never checked | — | fixed — it counts the paint |
| *Test user, 2026-10-06:* expected the list to show the latest addition; asked to name napkins | low | done — "Latest: …" on the row's second line; optional names (§3) |
| *Test user, 2026-10-06:* wanted everything copied to land in Napkin without switching to it | — | decided: not a clipboard history (§1), and Wayland serves the clipboard only to a focused window (§17); the global capture hotkey is the answer, next |
| *Test user, 2026-10-06:* could not find the way out of the trash — tried the mouse's Back button, then hunted, before seeing the "Trash" button was a toggle | medium | fixed — the trash list opens with "← Back to napkins" and the retention note; the mouse Back button and Alt+Left also leave (Alt+Left only while in the trash) |

**Fixed 2026-10-05**, from the list that stood here: the lightbox pages
through a napkin's images with ←/→ (Home/End to the ends, no wrap); a long
note's card holds only its first `kMeasureLimit` characters until it is edited,
and `text()` still answers with all of it (§7); the blob sweep walks the store
on a worker and decides on the UI thread (§8); the single-instance guard is a
real lock (§10); undo offers stack and name the napkin (§6).

**Known and not yet fixed**, carried forward honestly:

- Opening a long note **for editing** still pays `setPlainText` on the whole
  of it — 135 ms for 1.2 MB, 0.5 s for 10 MB — once, at the moment editing
  starts. Inherent to `QPlainTextEdit`; only a chunked or custom editor would
  remove it. So does every *forced* save of one (leaving the card, switching
  napkins, the window losing focus): those must have landed before they return.
- Peak memory while the board's previews are being made: decoding hundreds of
  multi-megapixel images on two workers grows glibc's arenas to ~550 MB before
  the idle trim hands it back (§12). Live data stays under 60 MB throughout.
- A thumbnail written by a worker for an image whose row is purged *while* the
  write is in flight survives until the next sweep (at the next launch).

---

## 21. The test that governs every future feature

> Does this make it easier to **put something in**, **find it**, **use it**,
> **keep it**, or **get rid of it**?

If no, it does not go in Napkin. The power is low friction, not feature count.

> **It's just a napkin. Put it down and move on.**
