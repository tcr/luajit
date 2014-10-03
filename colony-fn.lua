print('1..5')

function go ()
	local f = function () end

	f.hi = 5
	f.hi = f.hi + 5
	local a = f.hi

	print(a == 10 and 'ok' or 'not ok', 'functions can have properties')
end

go()
collectgarbage();


function test ()
end

if not test.undef then
	print('ok', 'undefined values dont break interpreter')
end

debug.setmetatable(function () end, {
	__index = function (ths, key)
		-- print('ok', ths, key)
		return 'ok\tfunctions can have __index'
	end
})

print(test.hello)
test.ok = 'ok'
print(test.ok, 'functions can still have properties')

debug.setmetatable(function () end, {
	__newindex = function (ths, key, value)
		if value == 'not ok' then
			print('ok', 'newindex can be called')
		else
			print('not ok')
		end
	end
})
test.newok2 = 'not ok'

print(rawget(test, 'ok'), 'rawget works')
-- TODO rawget?
