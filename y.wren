// TODO: there has to be a better way to define a product type
class WaitingFiber {
    fiber { _fiber }
    eventSet { _eventSet }

    construct new(fiber, eventSet) {
        _fiber = fiber
        _eventSet = eventSet
    }

    toString { _eventSet.deadline.toString }
}

class EventSet {
    deadline { _deadline }
    events { _events }

    construct new(deadline, events) {
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
        _waitingFibers = []
    }

    // runs in parent fiber
    do_run(block) {
        var topFiber = Fiber.new(block)

        // We need to pass `this`, so this call is special and can't be merged with the one in the
        // while loop.
        var waitingFiber = topFiber.call(this)

        while (waitingFiber || _waitingFibers.count > 0) {
            if (waitingFiber is WaitingFiber) {
                _waitingFibers.add(waitingFiber)
            } else if (waitingFiber == null) {
                // fine
            } else {
                // TODO: better error message
                Fiber.abort("fiber did not yield a WaitingFiber or null")
            }

            var now = System.clock
            var earliestDeadline
            var earliestDeadlineIndex
            for (i in 0..._waitingFibers.count) {
                var deadline = _waitingFibers[i].eventSet.deadline
                if (!earliestDeadline || deadline < earliestDeadline) {
                    earliestDeadline = deadline
                    earliestDeadlineIndex = i
                }
            }
            while (earliestDeadline > System.clock) {
                // spin
                // TODO: sleep instead of spinning
            }

            var readyFiber = _waitingFibers.removeAt(earliestDeadlineIndex).fiber
            waitingFiber = readyFiber.call()
        }
    }

    static yield(eventSet) {
        Fiber.yield(WaitingFiber.new(Fiber.current, eventSet))
    }

    start(block) {
        var fiber = Fiber.new(block)
        _waitingFibers.insert(0, WaitingFiber.new(fiber, EventSet.new(0, [])))
        Scheduler.yield(EventSet.new(0, []))
        return fiber
    }
}

Scheduler.run { |sched|
    System.print("1")
    sched.start {
        System.print("2")
        sched.start {
            System.print("3")
            sched.start {
                System.print("4")
            }
            System.print("a")
        }
        System.print("b")
    }
    Scheduler.yield(EventSet.new(System.clock + 0.5, []))
    System.print("c")
}
