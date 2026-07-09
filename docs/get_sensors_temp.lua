do
    if not dat then dat = {radiatorThresholdTemp=30, radiatorCriticalTemp=40} end
    if not topub then topub = {} end
    local getSensorsTemp = function(local_sensors)
        if #local_sensors == 3 then
            dat.sensors = local_sensors
--            print(dat.sensors[1]..' '..dat.sensors[2]..' '..dat.sensors[3])
        else
            dat.message = "No sensors found or count not enough: "..#local_sensors..". Fun heater forced shutdown."
            print(dat.message)
            table.insert(topub, {'message', dat.message})
        end
        local_sensors = nil
    end
    
--    local_sensors = {}
    get18b20(getSensorsTemp, pinSensors)
end
