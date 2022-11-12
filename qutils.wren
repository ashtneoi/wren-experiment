class Q {
    static expect(value) {
        if (!value) {
            Fiber.abort("value is %(value)")
        }
    }

    foreign static monotonicClock
}
