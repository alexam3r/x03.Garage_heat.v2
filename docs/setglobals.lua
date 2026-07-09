
topub = {}
--sens = 1
sent = {}
dat = { count = 45 }
dat.clntid = node.chipid()
print('Client: ',dat.clntid)
dat.topic = 'garage/heat'
print('Topic: ',dat.topic)
dat.nodetopic = dat.topic..'/'..dat.clntid
dat.brok = '10.0.0.1'
dat.user = 'MQTT_USER'
dat.pass = 'MQTT_PASS'

dat.logState = 'OFF'
dat.log = {}
dat.airTemp = 99
dat.hum = 99
dat.fanHeater = "ON"
dat.fanHeaterCoolerNow = "OFF"
dat.fanHeaterCoolerDelay = 20000
dat.fanHeaterCoolerTimer = tmr.create()
dat.fanHeaterLoadNow = "OFF"
dat.fanHeaterLoadTimer = tmr.create()
dat.fanHeaterLoadONLimit = 20000-math.floor(dat.fanHeaterCoolerDelay/2)
dat.fanHeaterLoadOFFLimit = math.floor(dat.fanHeaterCoolerDelay/2)
dat.targetSensorTemp = 5
dat.SensorTempDiff = 15
dat.SensorMaxTemp = dat.targetSensorTemp + dat.SensorTempDiff
dat.sensors = {99,99,99}
dat.targetAirTemp = 10
dat.calorifer = 'OFF'
dat.caloriferNow = 'OFF'
dat.radiatorTemp = 99
dat.radiatorThresholdTemp = 30
dat.radiatorCriticalTemp = 35

pinAirTemp = 2
pinSensors = 3
pinRadiatorTemp = 4
pinFanHeaterCooler = 5
pinSSR1 = 6
pinSSR2 = 7
pinRadiatorFan = 8

gpio.mode(pinFanHeaterCooler,gpio.OUTPUT)
gpio.mode(pinSSR1,gpio.OUTPUT)
gpio.write(pinSSR1,0)
gpio.mode(pinSSR2,gpio.OUTPUT)
gpio.write(pinSSR2,0)
gpio.mode(pinRadiatorFan,gpio.OUTPUT)


rtctime.set(0, 0)
dofile('mqttset.lua')
dofile('main.lua')
