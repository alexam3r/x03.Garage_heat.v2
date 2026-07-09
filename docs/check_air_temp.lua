do
    
    getAirTemp = function(airTemp)
    --        for k,v in pairs(airTemp) do print(k,v) end
        if #airTemp == 1 then    
            dat.airTemp = math.floor(airTemp[1]*10)/10-1
            log('airTemp: '..dat.airTemp)
            table.insert(topub, {'airTemp', dat.airTemp})
            if dat.airTemp > 0 then dat.fanHeaterCoolerDelay = 20000
            elseif dat.airTemp < 0 and dat.airTemp >= -5 then dat.fanHeaterCoolerDelay = 17500
            elseif dat.airTemp < -5 and dat.airTemp >= -10 then dat.fanHeaterCoolerDelay = 15000
            elseif dat.airTemp < -10 and dat.airTemp >= -15 then dat.fanHeaterCoolerDelay = 12500
            elseif dat.airTemp < -15 then dat.fanHeaterCoolerDelay = 10000 end
        else
            dat.message = "[Error get airTemp: d18b20 didn't answer]"
            print(dat.message)
            table.insert(topub, {'message', dat.message})
            dat.fanHeaterCoolerDelay = 20000
            table.insert(topub, {'calorifer', 'OFF'})
            calorifer('OFF')
        end
    
        if dat.calorifer == "ON" and dat.radiatorTemp < dat.radiatorCriticalTemp then
            if dat.airTemp > dat.targetAirTemp + 1 then caloriferOFF()
            elseif dat.airTemp < dat.targetAirTemp - 1 then caloriferON() end
        end
    
    end
    get18b20(getAirTemp, pinAirTemp)
end
