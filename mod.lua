function a (b)
	local a = -0x555
	return a % b
end

local lis = {-0xFF, -0xFE, -0xFE, -0xFF}
local c = {}
for i = 1,1000 do
	table.insert(c, a(lis[i % 4 + 1]))
end
for i = 1,1000 do
	io.write(tostring(c[i]))
end
