function a (b)
	return not b
end

local lis = {0, 1, 0, 0, 1, 2, 10, 100}
local c, d = {}, {}
for i = 1,90 do
	c[i] = a(lis[i % #lis + 1])
	d[i] = (lis[i % #lis + 1])
end

print(c[81], c[82], c[83], c[84], c[85], c[86], c[87], c[88])
print(d[81], d[82], d[83], d[84], d[85], d[86], d[87], d[88])
print('')