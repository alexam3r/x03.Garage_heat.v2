do
    if not xpcall( function() 
    if #dat.sensors == 3 then
        log(dat.sensors[1]..', '..dat.sensors[2]..', '..dat.sensors[3])

        if dat.fanHeater == "ON" then
            if dat.fanHeaterCoolerNow == 'ON' then
                if dat.sensors[3] >= dat.SensorMaxTemp then
                    dat.fanHeaterLoadONLimit = dat.fanHeaterLoadONLimit - 1000
                    if dat.fanHeaterLoadONLimit < dat.fanHeaterCoolerDelay/4 then dat.fanHeaterLoadONLimit = math.floor(dat.fanHeaterCoolerDelay/4) end
                    dat.fanHeaterLoadOFFLimit = math.floor(dat.fanHeaterCoolerDelay/1.5)
                    log('DEC. Interval='..dat.fanHeaterLoadONLimit)
                elseif dat.sensors[3] <= dat.SensorMaxTemp - 5 then
                    dat.fanHeaterLoadONLimit = dat.fanHeaterLoadONLimit + 1000
                    dat.fanHeaterLoadOFFLimit = math.floor(dat.fanHeaterCoolerDelay/2.5)
                    log('INC. Interval='..dat.fanHeaterLoadONLimit)
                else
                    dat.fanHeaterLoadOFFLimit = math.floor(dat.fanHeaterCoolerDelay/2)
                end
            end

            if dat.sensors[2] > dat.targetSensorTemp + 0.5 and dat.fanHeaterLoadNow == 'ON' then
                log('Heater OFF by sensor2: '..dat.sensors[2])
                table.insert(topub, {'sensor2', math.floor(dat.sensors[2]*10)/10})
                fanHeaterOFF()
            elseif dat.sensors[2] < dat.targetSensorTemp - 0.5 and dat.fanHeaterCoolerNow == 'OFF' and dat.fanHeaterLoadNow == 'OFF' then 
                log('Heater ON by sensor2: '..dat.sensors[2])
                table.insert(topub, {'sensor2', math.floor(dat.sensors[2]*10)/10})
                fanHeaterON() 
            end
        end 
    else
        dat.message = "No sensors found or count not enough: "..#dat.sensors..". Fun heater forced shutdown."
        print(dat.message)
        table.insert(topub, {'message', dat.message})
        fanHeaterOFF()
    end
    end, myerrorhandler ) then
        fanHeaterOFF()
    end
end
