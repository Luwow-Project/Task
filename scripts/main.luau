const task = require("@Luwow/task")

task.spawn(function() 
    print("This thread runs immediately!")
end)

task.defer(function() 
    print("This thread runs after the current resumption cycle!")
end)

task.delay(1, function() 
    print("This thread runs after 1 second has elapsed!")
end)

task.wait(2)
print("This will print after 2 seconds!")