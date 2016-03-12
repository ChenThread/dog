C = require "component"
T = require "term"
gpu = C.gpu

fname = ...

fp = io.open(fname, "rb")
magic = fp:read(5)
if magic ~= "OCGPU" then error("not a valid OCGPU image") end
bpp = string.byte(fp:read(1))
if bpp ~= 4 and bpp ~= 8 then error("not a valid OCGPU image") end
w = string.byte(fp:read(1))
h = string.byte(fp:read(1))
if w < 1 or h < 1 then error("not a valid OCGPU image") end
if w > 160 or h > 50 then error("not a valid OCGPU image") end
palsize = string.byte(fp:read(1))
if palsize < 0 or palsize > 16 then error("not a valid OCGPU image") end

palette = {}
for i = 1,16 do
	if i <= palsize then
		palette[i] = 0
		palette[i] = palette[i] + string.byte(fp:read(1)) * 0x1
		palette[i] = palette[i] + string.byte(fp:read(1)) * 0x100
		palette[i] = palette[i] + string.byte(fp:read(1)) * 0x10000
	else
		local v = math.floor((i*255+0.5)/17)
		palette[i] = 0x010101 * ((math.tointeger and math.tointeger(v)) or v)
	end
	gpu.setPaletteColor(i-1, palette[i])
end

for i = 1+16,256 do
	local v = i-(16+1)
	local b = (v % 5)
	local g = math.floor(v // 5) % 8
	local r = math.floor(v // (5*8))
	r = math.tointeger(math.floor(255*r/5))
	g = math.tointeger(math.floor(255*g/7))
	b = math.tointeger(math.floor(255*b/4))
	assert(r >= 0x00 and r <= 0xFF)
	assert(g >= 0x00 and g <= 0xFF)
	assert(b >= 0x00 and b <= 0xFF)
	palette[i] = 0
	palette[i] = palette[i] + r * 0x10000
	palette[i] = palette[i] + g * 0x100
	palette[i] = palette[i] + b * 0x1
	palette[i] = (math.tointeger(palette[i]) or palette[i])
end

T.clear()
while true do
	local mask = fp:read(1)
	if mask == "" or mask == nil then break end
	mask = string.byte(mask)
	if mask == 0x00 then error("reserved value for mask: 0x00") end
	if (mask & ~0x07) ~= 0 then error("reserved bits in mask") end

	-- if this fires you've probably got a suboptimal converter
	if (mask & 0x04) == 0 then error("currently no point in having a non-set block") end

	if (mask & 0x01) ~= 0 then
		local i = string.byte(fp:read(1))
		if i < 16 then
			gpu.setBackground(i, true)
		elseif bpp < 8 then
			error("cannot use 240 RGB in 4bpp mode")
		else
			gpu.setBackground(palette[i+1])
		end
	end
	if (mask & 0x02) ~= 0 then
		local i = string.byte(fp:read(1))
		if i < 16 then
			gpu.setForeground(i, true)
		elseif bpp < 8 then
			error("cannot use 240 RGB in 4bpp mode")
		else
			gpu.setForeground(palette[i+1])
		end
	end

	if (mask & 0x04) ~= 0 then
		local pos_x = string.byte(fp:read(1))
		local pos_y = string.byte(fp:read(1))
		local run_count = string.byte(fp:read(1))
		local reps
		local s = ""
		if run_count < 1 then error("should have at least one run") end
		for reps=1,run_count do
			local run_len = string.byte(fp:read(1))
			if run_len < 1 and reps == 1 then
				error("should have at least one byte in the first run")
			end

			local x
			for x=1,run_len do
				s = s .. utf8.char(0x2800 + string.byte(fp:read(1)))
			end

			if reps < run_count then
				-- XXX: untested!
				--error("untested code!")
				local skip_len = string.byte(fp:read(1))

				if skip_len == 0x80 or skip_len == 0x00 then
					error("reserved value for skip len")
				end

				if skip_len >= 0x80 then
					skip_len = skip_len - 0x100
				end

				if skip_len < 0 then
					for x=1,-skip_len do
						s = s .. unicode.char(0x28FF)
					end
				else
					for x=1,skip_len do
						s = s .. unicode.char(0x2800)
					end
				end
			end
		end

		gpu.set(pos_x, pos_y, s)
	end
end
fp:close()

-- TODO: wait for key
gpu.setBackground(0x000000)
gpu.setForeground(0xFFFFFF)

