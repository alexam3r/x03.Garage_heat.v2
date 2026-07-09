caloriferON = function()
    if not xpcall( function() 
    if dat.calorifer == 'ON' then
        gpio.write(pinSSR1,1); 
        dat.caloriferNow = "ON" 
        table.insert(topub, {'caloriferNow', dat.caloriferNow})
        log('CaloriferNow ON.')
    end
    end, myerrorhandler ) then
        gpio.write(pinSSR1,0)
    end
end

caloriferOFF = function()
    if not xpcall( function() 
    if dat.caloriferNow == 'ON' then
        gpio.write(pinSSR1,0)
        dat.caloriferNow = "OFF"
        table.insert(topub, {'caloriferNow', dat.caloriferNow})
        log('CaloriferNow OFF.')
    end
    end, myerrorhandler ) then
        gpio.write(pinSSR1,0)
    end
end 

fanHeaterON = function()
    if not xpcall( function()  
    if dat.fanHeaterCoolerNow ~= "ON" then 
        dat.fanHeaterCoolerNow = "ON"
        table.insert(topub, {'fanHeaterCoolerNow', dat.fanHeaterCoolerNow})
        gpio.write(pinFanHeaterCooler,1)
    end

    if dat.fanHeaterLoadNow ~= "ON" and dat.fanHeaterCoolerNow == 'ON' then 
        dat.fanHeaterLoadNow = "ON"
        table.insert(topub, {'fanHeaterLoadNow', dat.fanHeaterLoadNow})
        gpio.write(pinSSR2,1)
    end

    LoadTimerOFF = tmr:create()
    LoadTimerOFF:register(dat.fanHeaterLoadONLimit, tmr.ALARM_SEMI, function() 
        dat.fanHeaterLoadTimer = LoadTimerON
        dat.fanHeaterLoadTimer:interval(math.floor(dat.fanHeaterLoadOFFLimit))
        dat.fanHeaterLoadTimer:start()
        log('HeaterNow OFF. Interval: '..math.floor(dat.fanHeaterLoadOFFLimit))
        if dat.fanHeaterLoadNow == "ON" then 
            dat.fanHeaterLoadNow = "OFF"
            table.insert(topub, {'fanHeaterLoadNow', dat.fanHeaterLoadNow})
            table.insert(topub, {'sensor3', math.floor(dat.sensors[3]*10)/10})
            gpio.write(pinSSR2,0)
        end
    end)
    
    LoadTimerON = tmr:create()
    LoadTimerON:register(math.floor(dat.fanHeaterLoadOFFLimit), tmr.ALARM_SEMI, function() 
        dat.fanHeaterLoadTimer = LoadTimerOFF
        dat.fanHeaterLoadTimer:interval(dat.fanHeaterLoadONLimit)
        dat.fanHeaterLoadTimer:start()
        log('HeaterNow ON. Interval: '..dat.fanHeaterLoadONLimit)
        if dat.fanHeaterLoadNow ~= "ON" and dat.fanHeaterCoolerNow == 'ON' then 
            dat.fanHeaterLoadNow = "ON"
            table.insert(topub, {'fanHeaterLoadNow', dat.fanHeaterLoadNow})
            table.insert(topub, {'sensor3', math.floor(dat.sensors[3]*10)/10})
            gpio.write(pinSSR2,1)
        end
    end)
    
    dat.fanHeaterLoadTimer = LoadTimerOFF
    dat.fanHeaterLoadTimer:start()
    
    if dat.fanHeaterCoolerTimer:state() then
        dat.fanHeaterCoolerTimer:stop()
        dat.fanHeaterCoolerTimer:unregister()
    end
    end, myerrorhandler ) then
        gpio.write(pinSSR2,0)
        tmr.create():alarm(10000, tmr.ALARM_SINGLE,  function() gpio.write(pinFanHeaterCooler,0) end)
    end
end

fanHeaterOFF = function()
    if not xpcall( function() 
    if dat.fanHeaterLoadTimer then dat.fanHeaterLoadTimer:stop() dat.fanHeaterLoadTimer:unregister() end
    if LoadTimerOFF then LoadTimerOFF:unregister() LoadTimerOFF = nil end
    if LoadTimerON then LoadTimerON:unregister() LoadTimerON = nil end
    gpio.write(pinSSR2,0)
    dat.fanHeaterLoadNow = "OFF"
    table.insert(topub, {'fanHeaterLoadNow', dat.fanHeaterLoadNow})
    if not dat.fanHeaterCoolerTimer:state() then
        dat.fanHeaterCoolerTimer:register(dat.fanHeaterCoolerDelay, tmr.ALARM_SINGLE, function(t) 
            gpio.write(pinFanHeaterCooler,0)
            dat.fanHeaterCoolerNow = "OFF"
            table.insert(topub, {'fanHeaterCoolerNow', dat.fanHeaterCoolerNow})
            t:unregister()
            dat.fanHeaterLoadONLimit = 20000-math.floor(dat.fanHeaterCoolerDelay/2)
        end)
        dat.fanHeaterCoolerTimer:start()
        log('Heater OFF. Delay: '..tostring(dat.fanHeaterCoolerDelay))
    else
        dat.fanHeaterCoolerTimer:stop()
        dat.fanHeaterCoolerTimer:start()
    end
    end, myerrorhandler ) then
        gpio.write(pinSSR2,0)
        tmr.create():alarm(10000, tmr.ALARM_SINGLE,  function() gpio.write(pinFanHeaterCooler,0) end)
    end
end

get18b20 = function(call, pin)
    xpcall( function() 
    local ttable = {}
    local adrtbl = {}
    ow.setup(pin)
    ow.reset_search(pin)
    repeat
        local adr = ow.search(pin)
        if(adr ~= nil) then
            table.insert(adrtbl, adr)
        end
    until (adr == nil)
    ow.reset_search(pin)
    ow.setup(pin)
    for _, v in pairs(adrtbl) do
        ow.reset(pin)
        ow.select(pin, v)
        ow.write(pin, 0x44, 1)
    end
    v = nil

    tmr.create():alarm(750, tmr.ALARM_SINGLE, function (t) 
        local data, crc, t
        for _, v in pairs(adrtbl) do
            ow.reset(pin)
            ow.select(pin, v)
            ow.write(pin,0xBE,1)
            data = string.char(ow.read(pin))
            for i = 1, 8 do
                data = data .. string.char(ow.read(pin))
            end
            crc = ow.crc8(string.sub(data,1,8))
            if (crc == data:byte(9)) then
                t = (data:byte(1) + data:byte(2) * 256)
                if (t > 32767) then t = t - 65536 end
                t = t * 625 /10000
                table.insert(ttable, t)
            end
        end
        t = nil
        if call then call(ttable) end
    end)
    end, myerrorhandler )
end

log = function(str)
    xpcall( function() 
    print(str)
    if dat.logState == 'ON' then
        table.insert(dat.log, {'log', str, 0})
    end
    end, myerrorhandler )
end

function myerrorhandler( err )
   log( "ERROR:"..err )
end

dispatch = function()
    local nextc = 1
    local function ne()
        if nextc <= #threads then
--            print('no '..nextc)
            threads[nextc]()
            nextc = nextc + 1
        end
    end
    ne()
    tmr.create():alarm(1500, 1, function(t)
        if nextc <= #threads then
            ne()
        else
            t:stop()
            t:unregister()
            t = nil
            nextc, ne, threads = nil, nil, nil
        end
    end)
end 


dofile("check_air_temp.lua")


tmr.create():alarm(1000, tmr.ALARM_AUTO,  function()
    if dat.count %  10 == 0 then
        threads = {}
        table.insert(threads, function() dofile("get_sensors_temp.lua") end)
        if dat.fanHeaterCoolerNow == 'ON' or dat.count % 60 == 0 then 
            table.insert(threads, function() dofile("check_sensors.lua") end)
        end
        if dat.count % 60 == 0 then
            if #dat.sensors == 3 then
                table.insert(topub, {'sensor1', math.floor(dat.sensors[1]*10)/10})
                table.insert(topub, {'sensor2', math.floor(dat.sensors[2]*10)/10})
                if dat.fanHeaterLoadNow == 'OFF' then
                        table.insert(topub, {'sensor3', math.floor(dat.sensors[3]*10)/10})
                end
            end
            table.insert(threads, function() dofile("check_air_temp.lua") end)
            table.insert(threads, function() dofile("check_radiator_temp.lua") end)
        end
        if #threads > 0 then dispatch() end
    end

    dat.count = dat.count + 1
    if dat.count >= 60 then
        table.insert(topub, {dat.clntid..'/heap', node.heap()})
        local uptime = rtctime.get()
        table.insert(topub, {dat.clntid..'/uptime', uptime})
        dat.count = 0
    end
    if table.getn(dat.log) > 0 then
        local tp = table.remove(dat.log)
        table.insert(topub, {tp[1], tp[2]})
    end
    if (topub and #topub ~= 0) then dofile 'mqttpub.lua' end
end) 

