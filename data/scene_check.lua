-- The scene file's checker (loader 2.61.0, split from scene_file.lua): the rules of a scene file,
-- in plain Lua with no call into the game, so a program that writes scene files can check them with
-- the loader's own rules. It returns a table: check(doc, env) and the names the rules know.
--
-- doc is the file as read_json gives it: a JSON object or array is a table, an array's metatable is
-- ARRAY and null is NULL (both below). env holds what the file is checked against, each one optional
-- (left out, that check is skipped):
--   from          a label's name or a step's place ("steps[4]", "steps[2].choice[0].steps[1]") to
--                 start at
--   functions     the mod's functions by name (rq.scene.functions); a name missing is a problem
--   find_game     find_game(kind, v): the game's number for a quest, chara or item, or nil and why
--   save_loaded   save_loaded(): true when a save is loaded (a mod's quest has a number only then)
--   file_exists   file_exists(path): true when a picture's file is in the mod's folder
-- check gives c: c.problems ("place: what" texts, none when the file is good), and for the runner
-- c.kind (a step's or condition's kind by its table), c.place (a step's place), c.at (the step at a
-- place), c.trail (where a step sits, as { list, i } pairs from the file's steps down), c.labels
-- (name -> { trail, place }) and c.cast.

local FORMAT = 1 -- the newest format this loader reads; it reads every older one
local NULL = setmetatable({}, { __name = "json null", __tostring = function() return "null" end })
local ARRAY = { __name = "json array" }
local NAME = "^[%a_][%w_]*$"
local EXT = { png = true, PNG = true, jpg = true, JPG = true, jpeg = true, JPEG = true,
  webp = true, WEBP = true, gif = true, GIF = true }

---- helpers ----

local function is_array(v) return type(v) == "table" and getmetatable(v) == ARRAY end
local function is_object(v) return type(v) == "table" and getmetatable(v) == nil end
local function is_int(v) return math.type(v) == "integer" end
local function is_name(v) return type(v) == "string" and v:find(NAME) ~= nil end
local function unnull(v) if v == NULL then return nil end return v end
local function quote(s) return '"' .. tostring(s) .. '"' end

-- A picture path: in the mod's folder (not absolute, no "..", no ":"), of a kind rq.image reads.
local function is_path(v)
  if type(v) ~= "string" or v:find("^[/\\]") or v:find("..", 1, true) or v:find(":", 1, true) then
    return false
  end
  local ext = v:match("%.(%w+)$")
  return ext ~= nil and EXT[ext] == true
end

local function keys_of(t)
  local ks = {}
  for k in pairs(t) do ks[#ks + 1] = k end
  table.sort(ks, function(a, b) return tostring(a) < tostring(b) end)
  return ks
end

local function quoted_list(ks)
  local q = {}
  for i, k in ipairs(ks) do q[i] = quote(k) end
  return table.concat(q, ", ")
end

-- A step's place as the checks name it: steps[k], then any of .then[k], .else[k] and
-- .choice[r].steps[k], counted from 0.
local function is_place(v)
  if type(v) ~= "string" then return false end
  local rest = v:match("^steps%[%d+%](.*)$")
  while rest and rest ~= "" do
    rest = rest:match("^%.then%[%d+%](.*)$") or rest:match("^%.else%[%d+%](.*)$")
      or rest:match("^%.choice%[%d+%]%.steps%[%d+%](.*)$")
  end
  return rest == ""
end

---- the checks ----

local function add(c, place, msg)
  c.problems[#c.problems + 1] = place == "" and msg or (place .. ": " .. msg)
end

-- The simple types: nil when v fits, else what was expected.
local CHECK = {}
function CHECK.on(v) if v ~= true then return "expected true" end end
function CHECK.bool(v) if type(v) ~= "boolean" then return "expected true or false" end end
function CHECK.str(v) if type(v) ~= "string" then return "expected a text" end end
function CHECK.flag(v)
  if type(v) ~= "string" or v == "" then return "expected an event flag's name (a text)" end
end
function CHECK.nonempty(v)
  if type(v) ~= "string" or v == "" then return "expected a text that is not empty" end
end
function CHECK.int(v) if not is_int(v) then return "expected a whole number" end end
function CHECK.uint(v)
  if not is_int(v) or v < 0 then return "expected a whole number, 0 or more" end
end
function CHECK.speed(v)
  if not is_int(v) or v < -1 then return "expected a whole number, -1 or more" end
end
function CHECK.color(v)
  if not is_int(v) or v < 0 or v > 255 then return "expected a whole number from 0 to 255" end
end
function CHECK.chance(v)
  if not is_int(v) or v < 0 or v > 100 then return "expected a whole number from 0 to 100" end
end
function CHECK.number(v) if type(v) ~= "number" then return "expected a number" end end
function CHECK.name(v)
  if not is_name(v) then return "expected a name: letters, digits and _, not starting with a digit" end
end
function CHECK.value(v)
  local t = type(v)
  if not (t == "string" or t == "number" or t == "boolean" or v == NULL) then
    return "expected a text, a number, true, false or null"
  end
end

local COMPLEX = {}
local check_steps, check_cond

local function check_value(c, typ, v, place, trail)
  local simple = CHECK[typ]
  if simple then
    local m = simple(v)
    if m then add(c, place, m) end
    return
  end
  COMPLEX[typ](c, v, place, trail)
end

local function picture_at(c, v, place)
  c.pictures[#c.pictures + 1] = { v, place }
end

function COMPLEX.picture(c, v, place)
  if is_int(v) then
    if v < 0 then add(c, place, "expected a CG number, 0 or more") end
  elseif is_path(v) then
    picture_at(c, v, place)
  else
    add(c, place, "expected a picture: a CG number, or a path in the mod's folder ending in .png, "
      .. ".jpg, .jpeg, .webp or .gif (no \"..\", no \":\")")
  end
end

function COMPLEX.face(c, v, place)
  if is_int(v) then
    if v < 0 then add(c, place, "expected a face's CG number, 0 or more") end
  elseif is_name(v) then
    if c.cast[v] == nil then add(c, place, quote(v) .. " is not in the file's cast") end
  elseif is_path(v) then
    picture_at(c, v, place)
  else
    add(c, place, "expected a face: a CG number, a name of the file's cast, or a picture path")
  end
end

function COMPLEX.text(c, v, place)
  if type(v) == "string" then return end
  if is_array(v) and #v >= 1 then
    for k = 1, #v do
      if type(v[k]) ~= "string" then add(c, place .. "[" .. (k - 1) .. "]", "expected a text") end
    end
    return
  end
  add(c, place, "expected a text, or a list of pages (texts)")
end

function COMPLEX.args(c, v, place)
  if not is_array(v) then
    add(c, place, "expected a list of values")
    return
  end
  for k = 1, #v do
    local m = CHECK.value(v[k])
    if m then add(c, place .. "[" .. (k - 1) .. "]", m) end
  end
end

function COMPLEX.steps(c, v, place, trail) check_steps(c, v, place, trail) end
function COMPLEX.condition(c, v, place) check_cond(c, v, place) end

function COMPLEX.conditions(c, v, place)
  if not is_array(v) or #v == 0 then
    add(c, place, "expected a list of conditions")
    return
  end
  for k = 1, #v do check_cond(c, v[k], place .. "[" .. (k - 1) .. "]") end
end

function COMPLEX.fname(c, v, place)
  local m = CHECK.name(v)
  if m then add(c, place, m) else c.calls[#c.calls + 1] = { v, place } end
end

function COMPLEX.target(c, v, place)
  local m = CHECK.name(v)
  if m then add(c, place, m) else c.gotos[#c.gotos + 1] = { v, place } end
end

local function game_ref(kind)
  return function(c, v, place)
    if is_int(v) then
      if v < 1 then add(c, place, "expected a number, 1 or more") end
    elseif type(v) ~= "string" or v == "" then
      add(c, place, "expected a number or a name")
    else
      c.games[#c.games + 1] = { kind, v, place }
    end
  end
end
COMPLEX.quest = game_ref("quest")
COMPLEX.chara = game_ref("chara")
COMPLEX.item = game_ref("item")

local LAYERS = { wallpaper = true, background = true, portrait = true, mini = true, cg = true }
function COMPLEX.layers(c, v, place)
  if not is_object(v) or next(v) == nil then
    add(c, place, 'expected the layers to show, such as { "background": 5280 }')
    return
  end
  for _, k in ipairs(keys_of(v)) do
    if LAYERS[k] then
      COMPLEX.picture(c, v[k], place .. "." .. k)
    else
      add(c, place, "no layer " .. quote(k) .. ": wallpaper, background, portrait, mini or cg")
    end
  end
end

-- A place on the game's world map: one of the game's (世界地図データ取得 has 40, numbered from 1),
-- or a point of the mod's own with the name its tag shows.
local PLACES = 40
local MAP_POINT = { x = true, y = true, name = true }
function COMPLEX.place(c, v, place)
  if is_int(v) then
    if v < 1 or v > PLACES then
      add(c, place, "expected a place of the game's world map, 1 to " .. PLACES)
    end
    return
  end
  if not is_object(v) then
    add(c, place, "expected a place of the game's world map (1 to " .. PLACES
      .. '), or a point of its own, such as { "x": 620, "y": 340, "name": "Cidade" }')
    return
  end
  for _, k in ipairs(keys_of(v)) do
    if not MAP_POINT[k] then add(c, place, "a point has no field " .. quote(k) .. ": x, y and name") end
  end
  if not is_int(v.x) or v.x < 0 or v.x > 1023 then
    add(c, place .. ".x", "expected a whole number from 0 to 1023")
  end
  if not is_int(v.y) or v.y < 0 or v.y > 767 then
    add(c, place .. ".y", "expected a whole number from 0 to 767")
  end
  local m = CHECK.nonempty(v.name)
  if m then add(c, place .. ".name", m) end
end

local ROW = { text = "str", id = "int", comment = "str", r = "color", g = "color", b = "color",
  sound = "uint", ["if"] = "condition", steps = "steps", ["goto"] = "target", ["end"] = "value" }

function COMPLEX.choice(c, v, place, trail)
  local usage = 'expected a list of rows, or { "call": "a function" } for rows from the mod\'s Lua'
  if is_object(v) then
    for _, k in ipairs(keys_of(v)) do
      if k == "call" then
        COMPLEX.fname(c, v.call, place .. ".call")
      elseif k == "args" then
        COMPLEX.args(c, v.args, place .. ".args")
      else
        add(c, place, 'rows from a function take "call" and "args", not ' .. quote(k))
      end
    end
    if v.call == nil then add(c, place, usage) end
    return
  end
  if not is_array(v) or #v == 0 then
    add(c, place, usage)
    return
  end
  local ids, picks = {}, 0
  for k = 1, #v do
    local row, rp, id = v[k], place .. "[" .. (k - 1) .. "]", nil
    if row == "-" then -- a separator
    elseif type(row) == "string" then
      picks, id = picks + 1, k
    elseif is_object(row) then
      picks = picks + 1
      for _, key in ipairs(keys_of(row)) do
        local typ = ROW[key]
        if typ then
          check_value(c, typ, row[key], rp .. "." .. key, trail)
        else
          add(c, rp, "a row has no field " .. quote(key))
        end
      end
      if row.text == nil then add(c, rp, 'a row needs "text"') end
      local ways = (row.steps ~= nil and 1 or 0) + (row["goto"] ~= nil and 1 or 0)
        + (row["end"] ~= nil and 1 or 0)
      if ways > 1 then add(c, rp, 'a row goes one way: "steps", "goto" or "end"') end
      id = is_int(row.id) and row.id or k
    else
      add(c, rp, 'expected a row: a text, "-", or an object with "text"')
    end
    if id then
      if ids[id] then
        add(c, rp, "the id " .. id .. " is also the id of row " .. ids[id] .. " (a row's id is its place, "
          .. "from 1, unless it has one)")
      else
        ids[id] = k - 1
      end
    end
  end
  if picks == 0 then add(c, place, "no row to pick") end
end

-- Each step: its main value's type, its options, and an option it needs.
local LINE = { text = "text", name = "str", hide_face = "bool", page = "bool", wait = "bool" }
local PICTURE = { effect = "uint", time = "uint" }
local STEP = {
  say = { "face", LINE, need = "text" },
  think = { "face", LINE, need = "text" },
  narrate = { "text", { paper = "bool", page = "bool", wait = "bool" } },
  clear_text = { "on" },
  speed = { "speed" },
  background = { "picture", PICTURE },
  wallpaper = { "picture", PICTURE },
  mask_wallpaper = { "picture", PICTURE },
  portrait = { "picture", PICTURE },
  mini = { "picture", PICTURE },
  cg = { "picture", PICTURE },
  background_in = { "picture", { time = "uint" } },
  background_out = { "on", { time = "uint" } },
  set = { "layers", { effect = "uint" } },
  black = { "on", PICTURE },
  white = { "on", PICTURE },
  clear = { "on" },
  flash = { "on", { time = "uint" } },
  shake = { "on", { mode = "uint" } },
  shake_face = { "on", { mode = "uint" } },
  shake_portrait = { "on", { mode = "uint" } },
  music = { "uint", { effect = "int", time = "int" } },
  sound = { "uint" },
  jingle = { "uint", { wait = "bool" } },
  wait_click = { "on" },
  world_map = { "place" },
  choice = { "choice", { into = "name", x = "int", y = "int" } },
  ["if"] = { "condition", { ["then"] = "steps", ["else"] = "steps" }, need = "then" },
  flag = { "flag" },
  unflag = { "flag" },
  call = { "fname", { args = "args", into = "name" } },
  label = { "name" },
  ["goto"] = { "target" },
  ["end"] = { "value" },
  cmd = { "nonempty", { args = "args" } },
  note = { "str" },
}

local function check_step(c, st, place, trail)
  if not is_object(st) then
    add(c, place, 'expected a step: an object such as { "say": 2000, "text": "Hello." }')
    return
  end
  local all, kinds = keys_of(st), {}
  for _, k in ipairs(all) do
    if STEP[k] then kinds[#kinds + 1] = k end
  end
  if #kinds == 0 then
    add(c, place, #all == 0 and "an empty step"
      or ("not a step: no step is named " .. quoted_list(all)))
    return
  end
  if #kinds > 1 then
    add(c, place, "two steps in one object: " .. quoted_list(kinds) .. " (one step per object)")
    return
  end
  local kind = kinds[1]
  local spec = STEP[kind]
  c.kind[st], c.place[st], c.trail[st], c.at[place] = kind, place, trail, st
  check_value(c, spec[1], st[kind], place .. "." .. kind, trail)
  local opts = spec[2] or {}
  for _, k in ipairs(all) do
    if k ~= kind then
      if opts[k] then
        check_value(c, opts[k], st[k], place .. "." .. k, trail)
      else
        add(c, place, quote(kind) .. " has no field " .. quote(k))
      end
    end
  end
  if spec.need and st[spec.need] == nil then
    add(c, place, quote(kind) .. " needs " .. quote(spec.need))
  end
  if kind == "label" and is_name(st.label) then
    local old = c.labels[st.label]
    if old then
      add(c, place, "the label " .. quote(st.label) .. " is also at " .. old.place)
    else
      c.labels[st.label] = { trail = trail, place = place }
    end
  end
end

-- trail: where the list sits, as { list, i } pairs from the file's steps down; each step and
-- label keeps its own, and a goto or a start elsewhere rebuilds the runner's stack from it.
function check_steps(c, list, place, trail)
  if not is_array(list) then
    add(c, place, "expected a list of steps")
    return
  end
  for k = 1, #list do
    local t = {}
    for n, x in ipairs(trail) do t[n] = x end
    t[#t + 1] = { list = list, i = k }
    check_step(c, list[k], place .. "[" .. (k - 1) .. "]", t)
  end
end

local COND = { seen = "flag", var = "name", call = "fname", ["not"] = "condition",
  all = "conditions", any = "conditions", cleared = "quest", recruited = "chara",
  in_party = "chara", has_item = "item", gold = "uint", chance = "chance" }
local COND_OPTS = { var = { is = "value", at_least = "number", at_most = "number" },
  call = { args = "args" } }

function check_cond(c, v, place)
  if not is_object(v) then
    add(c, place, 'expected a condition, such as { "seen": "mymod:met" }')
    return
  end
  local all, kinds = keys_of(v), {}
  for _, k in ipairs(all) do
    if COND[k] then kinds[#kinds + 1] = k end
  end
  if #kinds == 0 then
    add(c, place, #all == 0 and "an empty condition"
      or ("not a condition: no condition is named " .. quoted_list(all)))
    return
  end
  if #kinds > 1 then
    add(c, place, "two conditions in one object: " .. quoted_list(kinds)
      .. ' (put them in { "all": [...] } or { "any": [...] })')
    return
  end
  local kind = kinds[1]
  c.kind[v] = kind
  check_value(c, COND[kind], v[kind], place .. "." .. kind)
  local opts = COND_OPTS[kind] or {}
  for _, k in ipairs(all) do
    if k ~= kind then
      if opts[k] then
        check_value(c, opts[k], v[k], place .. "." .. k)
      else
        add(c, place, quote(kind) .. " has no field " .. quote(k))
      end
    end
  end
  if kind == "var" and v.is == nil and v.at_least == nil and v.at_most == nil then
    add(c, place, '"var" needs "is", "at_least" or "at_most"')
  end
end

local function check_cast(c, v)
  if not is_object(v) then
    add(c, "cast", 'expected names for faces, such as { "mira": { "face": 2816 } }')
    return
  end
  for _, name in ipairs(keys_of(v)) do
    local e, p = v[name], "cast." .. name
    if not is_name(name) then
      add(c, "cast", quote(name) .. " is not a name: letters, digits and _, not starting with a digit")
    elseif not is_object(e) then
      add(c, p, 'expected { "face": a CG number or a picture path, "name": the name plate }')
    else
      for _, k in ipairs(keys_of(e)) do
        if k == "face" then
          if is_name(e.face) then
            add(c, p .. ".face", "expected a CG number or a picture path")
          else
            COMPLEX.picture(c, e.face, p .. ".face")
          end
        elseif k == "name" then
          check_value(c, "str", e.name, p .. ".name")
        else
          add(c, p, "a cast entry has no field " .. quote(k))
        end
      end
      if e.face == nil then add(c, p, 'a cast entry needs "face"') end
    end
  end
end

-- What a schema cannot say: the labels, where to start, the functions, the game's names and the
-- pictures' files.
local function check_refs(c, env)
  for _, g in ipairs(c.gotos) do
    if not c.labels[g[1]] then add(c, g[2], "no label " .. quote(g[1]) .. " in the file") end
  end
  local from = env.from
  if from ~= nil then
    if is_place(from) then
      if not c.at[from] then add(c, "", "no step at " .. from .. " to start at") end
    elseif not (type(from) == "string" and c.labels[from]) then
      add(c, "", "no label " .. quote(from) .. " to start at")
    end
  end
  local fns = env.functions
  if fns then
    for _, x in ipairs(c.calls) do
      if not fns[x[1]] then
        add(c, x[2], "no function " .. quote(x[1]) .. ": the mod names its functions with "
          .. "rq.scene.functions")
      end
    end
  end
  if env.find_game then
    local loaded
    for _, g in ipairs(c.games) do
      local no, why = env.find_game(g[1], g[2])
      if not no then
        -- A mod's quest has its number only in a save that has it.
        if g[1] ~= "quest" then
          add(c, g[3], why)
        else
          if loaded == nil then loaded = env.save_loaded ~= nil and env.save_loaded() end
          if loaded then add(c, g[3], why) end
        end
      end
    end
  end
  if env.file_exists then
    local seen = {}
    for _, p in ipairs(c.pictures) do
      if not seen[p[1]] then
        seen[p[1]] = true
        if not env.file_exists(p[1]) then add(c, p[2], "no file " .. p[1] .. " in the mod's folder") end
      end
    end
  end
end

local function check_doc(doc, env)
  env = env or {}
  local c = { problems = {}, kind = {}, place = {}, at = {}, trail = {}, labels = {}, gotos = {},
    calls = {}, games = {}, pictures = {}, cast = {} }
  if not is_object(doc) then
    add(c, "", 'expected an object with "format" and "steps"')
    return c
  end
  local fmt = doc.format
  if fmt == nil then
    add(c, "", 'needs "format": 1')
  elseif is_int(fmt) and fmt > FORMAT then
    add(c, "format", ("format %d is newer than this RQ MOD reads (%d): update RQ MOD"):format(fmt, FORMAT))
    return c
  elseif fmt ~= FORMAT then
    add(c, "format", "expected 1")
  end
  if is_object(doc.cast) then c.cast = doc.cast end
  for _, k in ipairs(keys_of(doc)) do
    if k == "$schema" then
      check_value(c, "str", doc[k], k)
    elseif k == "once" then
      check_value(c, "flag", doc.once, "once")
    elseif k == "cast" then
      check_cast(c, doc.cast)
    elseif k == "steps" then
      check_steps(c, doc.steps, "steps", {})
    elseif k ~= "format" then
      add(c, "", "a scene file has no field " .. quote(k))
    end
  end
  if doc.steps == nil then add(c, "", 'needs "steps"') end
  check_refs(c, env)
  return c
end

return {
  check = check_doc,
  FORMAT = FORMAT, NULL = NULL, ARRAY = ARRAY,
  is_array = is_array, is_object = is_object, is_int = is_int, is_name = is_name,
  is_place = is_place, is_path = is_path, unnull = unnull, quote = quote,
  -- What the rules know, for a program's lists: each step's main value's type, its options and
  -- the option it needs; each condition's type and options; a choice row's fields; the layers;
  -- how many places the game's world map has.
  STEP = STEP, COND = COND, COND_OPTS = COND_OPTS, ROW = ROW, LAYERS = LAYERS, PLACES = PLACES,
}
