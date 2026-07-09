do
local tp, punow


xpcall( function() 
    punow = function()
        if topub and #topub ~= 0 and wifi.sta.getip() and dat.broker then
            tp = table.remove(topub)
            tp[2] = tp[2] or ""
            tp[3] = tp[3] or 1
            m:publish(dat.topic..'/'..tp[1], tp[2], 1, tp[3], punow)
            --print("Publish: "..dat.topic..'/'..tp[1], tp[2])
        else
            tp, punow, uptime = nil, nil, nil
        end
    end
    punow()
end, myerrorhandler )
end 
