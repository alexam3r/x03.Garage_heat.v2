do
local itm
local retartnow = function()
	rtcmem.write32(0, 501)
	table.insert(topub, {dat.clntid..'/ip', wifi.sta.getip(),0})
	dofile 'mqttpub.lua'
    gpio.write(pinSSR1,0)
    gpio.write(pinSSR2,0)
	tmr.create():alarm(1500, 0, function() node.restart() end)
end

--print('analise:', #btbl)
if btbl and #btbl ~= 0 then
	while #btbl ~= 0 do
		itm = table.remove(btbl)
        local ltop = string.match(itm[1],"./(%w+)$") 

        if ltop == "fanHeater" then
            if itm[2] ~= dat.fanHeater then
                dat.fanHeater = itm[2]
                if dat.fanHeater == 'ON' then
                    dofile("check_sensors.lua")
                else
                    fanHeaterOFF()
                end
            end
            
        end

        if ltop == "targetSensorTemp" then
            if itm[2] ~= dat.targetSensorTemp then
                dat.targetSensorTemp = tonumber(itm[2])
                table.insert(topub, {'targetSensorTempNow', dat.targetSensorTemp})
                dat.SensorMaxTemp = dat.targetSensorTemp + dat.SensorTempDiff
-- dofile("check_sensors.lua")
            end
        end 

        if ltop == "SensorTempDiff" then
            if tonumber(itm[2]) >= 5 and tonumber(itm[2]) <= 50 then
                dat.SensorMaxTemp = dat.targetSensorTemp + tonumber(itm[2])
                log('SensorMaxTemp='..dat.SensorMaxTemp)
            else
                log('SensorTempDiff out of range. CurrentMaxTemp:'..dat.SensorMaxTemp)
            end
        end 

        if ltop == "calorifer" then
            if itm[2] ~= dat.calorifer then
                dat.calorifer = itm[2]
                if dat.calorifer == "ON" then dofile("check_air_temp.lua") else caloriferOFF() end
            end 
        end 

        if ltop == "targetAirTemp" then
            if itm[2] ~= dat.targetAirTemp then
                dat.targetAirTemp = tonumber(itm[2])
                table.insert(topub, {'targetAirTempNow', dat.targetAirTemp})
--                if dat.calorifer == "ON" then dofile("check_air_temp.lua") end
            end
        end 

        if ltop == "logState" and itm[2] ~= dat.logState then
            dat.logState = itm[2]
        end 

        
        if ltop == 'ide' then
            retartnow()
        end 
        if ltop == 'restart' then node.restart() end 
	end
	brbl = nil
end
end
