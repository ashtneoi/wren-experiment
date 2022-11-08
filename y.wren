import "scheduler" for Task, Scheduler
import "qutils" for Q

Scheduler.run { |sched|
    // 0.0
    System.print("1")
    sched.start {
        // 0.0
        System.print("2")
        sched.add {
            // 0.0
            System.print("3")
            sched.start {
                // 0.0
                System.print("4")
            }
            // 0.0
            System.print("5")
            Scheduler.sleep(0.8)
            // 0.8
            System.print("8")
        }.wait(System.clock + 0.4)
        // 0.4
        System.print("7")
    }
    // 0.0
    Scheduler.sleep(0.2)
    // 0.2
    System.print("6")
}

System.print()

Scheduler.run { |sched|
    System.print("1")
    var a = sched.add {
        Scheduler.sleep(0.2)
        System.print("2")
    }
    var b = sched.add {
        Scheduler.sleep(0.4)
        System.print("4")
    }
    var c = sched.add {
        Scheduler.sleep(0.6)
        System.print("5")
    }
    Task.waitAny(System.clock + 0.8, [a, b, c])
    System.print("3")
}

System.print()

Scheduler.run { |sched|
    System.print("1")
    var a = sched.add {
        Scheduler.sleep(0.2)
        System.print("2")
    }
    var b = sched.add {
        Scheduler.sleep(0.4)
        System.print("3")
    }
    var c = sched.add {
        Scheduler.sleep(0.8)
        System.print("5")
    }
    Task.waitAll(System.clock + 0.6, [a, b, c])
    System.print("4")
}
