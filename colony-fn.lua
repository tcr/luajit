print('hi')
function go ()
	local f = function () end

	f.hi = 5
	f.hi = f.hi + 5
	a = f.hi

	print(a)
end

go()
collectgarbage();

print(go.prototype)

print('ok')