import "qutils" for Q

class Event {
}

class TaskWaitEvent is Event {
    construct new(task) {
        _task = task
    }

    occurred { _task.fiber.isDone }
}

class AllTaskWaitEvent is Event {
    construct new(tasks) {
        _tasks = tasks
    }

    occurred { _tasks.all {|t| t.fiber.isDone} }
}

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

    wait(deadline) {
        Fiber.yield(WakeSpec.new(deadline, [TaskWaitEvent.new(this)]))
    }

    ready {
        if (!_wakeSpec) {
            return true
        } else if(_wakeSpec.events.any {|e| e.occurred}) {
            return true
        } else if (_wakeSpec.deadline <= System.clock) {
            return true
        } else {
            return false
        }
    }

    static waitAny(deadline, tasks) {
        Fiber.yield(WakeSpec.new(deadline, tasks.map {|t| TaskWaitEvent.new(t)}.toList))
    }

    static waitAll(deadline, tasks) {
        Fiber.yield(WakeSpec.new(deadline, [AllTaskWaitEvent.new(tasks)]))
    }
}

// The first event to occur triggers a wakeup. The deadline also triggers a wakeup if no event has
// occurred by the time it expires.
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
        return new().doRun(block)
    }

    // runs in parent fiber
    construct new() {
        _tasks = []
        _current = null
    }

    // runs in parent fiber
    doRun(block) {
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

            var chosenTask

            // vvv FIXME: Do this without spinning.
            var now = System.clock
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
            // ^^^ FIXME

            Q.expect(chosenTask)
            _current = chosenTask
            currentWakeSpec = chosenTask.fiber.call()
        }

        Q.expect(_tasks.count == 0)
    }

    add(block) {
        var task = Task.new(block)
        task.wakeSpec = WakeSpec.new(0, [])
        _tasks.add(task)
        return task
    }

    start(block) {
        var task = Task.new(block)
        task.wakeSpec = WakeSpec.new(0, [])
        _tasks.insert(0, task)
        Fiber.yield(WakeSpec.new(0, []))
        return task
    }

    static sleep(duration) {
        Fiber.yield(WakeSpec.new(System.clock + duration, []))
    }
}
