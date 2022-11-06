class Q {
    static expect(value) {
        if (!value) {
            Fiber.abort("value is %(value)")
        }
    }
}

class Event {
}

class TaskWaitEvent is Event {
    construct new(task) {
        _task = task
    }

    occurred { _task.fiber.isDone }
}

// class TaskSet {
//     // TODO: move the earliest-deadline logic here?
// }

// In order to pass fiber references around and be able to wait on them, we need to wrap them in
// tasks, since we need to map a fiber/task to its wake deadline (etc.) and Maps can't have fibers
// as keys.
class Task {
    fiber { _fiber }
    wakeSpec { _wakeSpec }

    wakeSpec=(value) { _wakeSpec = value }

    construct new(block) {
        _fiber = Fiber.new(block)
    }
}

class WakeSpec {
    deadline { _deadline }
    events { _events }

    construct new(deadline, events) {
        Q.expect(deadline || events.count > 0)
        _deadline = deadline
        _events = events
    }
}

class Scheduler {
    // runs in parent fiber
    static run(block) {
        return new().do_run(block)
    }

    // runs in parent fiber
    construct new() {
        _tasks = []
        _current = null
    }

    // runs in parent fiber
    do_run(block) {
        var topTask = Task.new(block)
        _tasks.insert(0, topTask)
        _current = topTask

        // We need to pass `this`, so this call is special and can't be merged with the one in the
        // while loop.
        var currentWakeSpec = topTask.fiber.call(this)

        while (_tasks.count > 0) {
            if (_current.fiber.isDone) {
                _tasks.remove(_current)
                if (_tasks.count == 0) {
                    break
                }
            } else {
                Q.expect(currentWakeSpec)
                _current.wakeSpec = currentWakeSpec
                _current = null
            }

            Q.expect(_tasks.count > 0)

            var now = System.clock
            var chosenTask
            while (!chosenTask) {
                for (task in _tasks) {
                    Q.expect(task.wakeSpec)
                    if (task.wakeSpec.events.any {|e| e.occurred}) {
                        chosenTask = task
                        break
                    } else if (task.wakeSpec.deadline) {
                        if (task.wakeSpec.deadline <= System.clock) {
                            chosenTask = task
                            break
                        }
                    }
                }
            }
            _current = chosenTask
            currentWakeSpec = chosenTask.fiber.call()
        }

        Q.expect(_tasks.count == 0)
    }

    start(block) {
        var task = Task.new(block)
        task.wakeSpec = WakeSpec.new(0, [])
        _tasks.insert(0, task)
        Fiber.yield(WakeSpec.new(0, []))
        return task
    }

    wait(deadline, task) {
        Fiber.yield(WakeSpec.new(deadline, [TaskWaitEvent.new(task)]))
    }

    static sleep(duration) {
        Fiber.yield(WakeSpec.new(System.clock + duration, []))
    }
}

Scheduler.run { |sched|
    System.print("1")
    sched.start {
        System.print("2")
        sched.wait(1, sched.start {
            System.print("3")
            sched.start {
                System.print("4")
            }
            System.print("5")
            Scheduler.sleep(2)
            System.print("8")
        })
        System.print("7")
    }
    Scheduler.sleep(0.5)
    System.print("6")
}
