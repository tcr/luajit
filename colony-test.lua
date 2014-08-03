local silent, tapi = false, 0
function tap (n) print('1..' .. tostring(n)); end
function ok (cond, why) if not silent then if not cond then io.write('not '); end print('ok ' .. tostring(tapi) .. ' - ' .. tostring(why)); end tapi = tapi + 1; end

tap(20)

debug.setmetatable('', {__add = function () print('at last'); return 'hello' end})
local a, b = '0', '2'
print(a + b)

if 1 then return end

function test ()
	local zero = 0
	local one = 1
	local nulstr = ""
	local astr = "hello world"

	ok(tostring(0/0) == 'NaN', 'nan is NaN not nan')
	ok((not zero) == true, '0 is falsy')
	if zero then
		ok(false, '0 is falsy in if')
	else
		ok(true, '0 is falsy in if')
	end
	ok((not nulstr) == true, '"" is falsy')
	if nulstr then
		ok(false, '"" is falsy in if')
	else
		ok(true, '"" is falsy in if')
	end
	ok(tonumber(true) == 1, 'can cast boolean to number')
	ok((zero or one) == 1, 'shortcut 0 in || operator')
	ok((zero and one) == 0, 'shortcut 0 in && operator')

	-- local a = {}
	-- setmetatable(a, { __tovalue = function () return 5; end })
	-- if not pcall(function ()
	-- 	ok(a + 5 == 10, 'can arithmetic objects with __tovalue in arith')
	-- end) then
	-- 	ok(false, 'can arithmetic objects with __tovalue in arith')
	-- end

	local five, six = '5', '6'

	local a = {}
	a[five] = 5
	a[5] = 2
	ok(a[five] == 2, 'numerical keys are converted')

	local a = {}
	a[5] = 2
	a[five] = 5
	ok(a[five] == 5, 'numerical keys (on arrays) are converted')

	local a = {1,2,3,4,5,0}
	a[six] = 1
	ok(a[6] == 1, 'numerical keys (on preallocated arrays) are converted')

	local a = {}
	a['5.2'] = 5
	a[5.2] = 2
	ok(a['5.2'] == 2, 'non-integral keys are converted')


	-- 5.2 options

	ok(debug.getinfo(function (b, c, d) end, 'u').nparams == 3, 'nparams implemented')

	debug.setmetatable(nil, {__lt = function () return true; end })
	if not pcall(function ()
		ok(nil < 5, 'nil comparsions allowed')
	end) then
		ok(false, 'nil comparsions allowed')
	end

	if not pcall(function ()
		debug.setmetatable(0, {__lt = function () return true; end })
		debug.setmetatable('', {__lt = function () return true; end })
		ok(5 < '6', 'comparison between differing types works')
	end) then
		ok(false, 'comparison between differing types works')
	end


	-- check for regressions

	ok((not astr) == false, '"hello world" is not falsy')
	if astr then
		ok(true, '"hello world" is not falsy in if')
	else
		ok(false, '"hello world" is not falsy in if')
	end
	a['nan'] = 1
	a['-nan'] = 1
	ok(true, 'nan and -nan are allowed')
	a['inf'] = 1
	a['-inf'] = 1
	ok(true, 'inf and -inf are allowed')
	a['00'] = 1
	a[0] = 2
	ok(a['00'] == 1, 'numbers with leading 0 are parsed fully for numericness')
end

-- force JIT
for i=1,10000 do
	silent = (i ~= 5000)
	tapi = 1
	test()
end
