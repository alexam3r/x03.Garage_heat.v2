do
    if not dat then dat = {radiatorThresholdTemp=30, radiatorCriticalTemp=40} end
    if not topub then topub = {} end
    getRadiatorTemp = function(radiatorTemp)
        if #radiatorTemp == 1 then
            table.insert(topub, {'radiatorTemp', math.floor(radiatorTemp[1]*10)/10})
            dat.radiatorTemp = radiatorTemp[1]
            log("radiatorTemp: "..dat.radiatorTemp)
            radiatorTemp = nil
--            print(dat.radiatorTemp)
            if tonumber(dat.radiatorTemp) > dat.radiatorThresholdTemp then
                dat.message = "Radiator temperature: "..(math.floor(dat.radiatorTemp*10)/10).."C. Radiator FAN activated!"
                print(dat.message)
                table.insert(topub, {'message', dat.message})
                gpio.write(pinRadiatorFan,1)
            else
                gpio.write(pinRadiatorFan,0)
            end
            if tonumber(dat.radiatorTemp) > dat.radiatorCriticalTemp then
                dat.message = "Radiator temperature over "..dat.radiatorCriticalTemp.."C. CaloriferNow set OFF. Current temp: "..math.floor(dat.radiatorTemp*10)/10
                print(dat.message)
                table.insert(topub, {'message', dat.message})
                caloriferOFF()
            end
        else
            dat.radiatorTemp = 99
            table.insert(topub, {'radiatorTemp', dat.radiatorTemp})
            dat.message = "No sensors found on radiator. Calorifer set OFF."
            print(dat.message)
            table.insert(topub, {'message', dat.message})
            dat.calorifer = 'OFF'
            table.insert(topub, {'calorifer', dat.calorifer})
            caloriferOFF()
        end
        radiatorTemp = nil
    end
    
--    radiatorTemp = {}
    get18b20(getRadiatorTemp, pinRadiatorTemp)
    
end
