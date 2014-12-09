print('1..13')

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
print(rawget(test, 'undef') == nil and 'ok' or 'not ok', 'rawget with nil works')

debug.setmetatable(function () end, {
	__newindex = function (ths, key, value)
		print('not ok')
	end
})
rawset(test, 'newthing', 'ok')
print(test.newthing, 'rawset doesnt throw not ok')
rawset(test, 'newthing', 'ok')
print(test.newthing, 'rawset doesnt throw not ok on reassign')


-- bug test
debug.setmetatable(function () end, nil)
function noise () end
noise.ok = 5
print(next(noise, nil), 'next should not freeze, should return first key')

collectgarbage()
print(rawget(error, 'happy') == nil and 'ok' or 'not ok')

function a () end
a.ok = 'ok'
print(rawget(a, 'ok'))

print(next(error, nil) == nil and 'ok' or 'not ok')