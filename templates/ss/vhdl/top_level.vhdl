--
-- Copyright (C) 2009-2014 Chris McClelland
--
-- This program is free software: you can redistribute it and/or modify
-- it under the terms of the GNU Lesser General Public License as published by
-- the Free Software Foundation, either version 3 of the License, or
-- (at your option) any later version.
--
-- This program is distributed in the hope that it will be useful,
-- but WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
-- GNU Lesser General Public License for more details.
--
-- You should have received a copy of the GNU Lesser General Public License
-- along with this program.  If not, see <http://www.gnu.org/licenses/>.
--
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity top_level is
	generic (
		NUM_DEVS       : integer := 1
	);
	port(
		sysClk_in      : in    std_logic;  -- system clock
		
		-- USB interface -----------------------------------------------------------------------------
		serClk_in      : in    std_logic;  -- serial clock (async to sysClk_in)
		serData_in     : in    std_logic;  -- serial data in
		serData_out    : out   std_logic;  -- serial data out

		-- Peripheral interface ----------------------------------------------------------------------
		spiClk_out     : out   std_logic;
		spiData_out    : out   std_logic;
		spiData_in     : in    std_logic;
		spiCS_out      : out   std_logic_vector(NUM_DEVS-1 downto 0)
	);
end entity;

architecture structural of top_level is
	-- Channel read/write interface -----------------------------------------------------------------
	signal chanAddr   : std_logic_vector(6 downto 0);  -- the selected channel (0-127)

	-- Host >> FPGA pipe:
	signal h2fData    : std_logic_vector(7 downto 0);  -- data lines used when the host writes to a channel
	signal h2fValid   : std_logic;                     -- '1' means "on the next clock rising edge, please accept the data on h2fData"
	signal h2fReady   : std_logic;                     -- channel logic can drive this low to say "I'm not ready for more data yet"

	-- Host << FPGA pipe:
	signal f2hData    : std_logic_vector(7 downto 0);  -- data lines used when the host reads from a channel
	signal f2hValid   : std_logic;                     -- channel logic can drive this low to say "I don't have data ready for you"
	signal f2hReady   : std_logic;                     -- '1' means "on the next clock rising edge, put your next byte of data on f2hData"
	-- ----------------------------------------------------------------------------------------------
begin
	-- CommFPGA module
	comm_fpga_ss : entity work.comm_fpga_ss
		port map(
			clk_in       => sysClk_in,
			reset_in     => '0',
			
			-- USB interface
			serClk_in    => serClk_in,
			serData_in   => serData_in,
			serData_out  => serData_out,

			-- DVR interface -> Connects to application module
			chanAddr_out => chanAddr,
			h2fData_out  => h2fData,
			h2fValid_out => h2fValid,
			h2fReady_in  => h2fReady,
			f2hData_in   => f2hData,
			f2hValid_in  => f2hValid,
			f2hReady_out => f2hReady
		);

	-- Switches & LEDs application
	spi_talk_app : entity work.spi_talk
		generic map (
			NUM_DEVS     => NUM_DEVS
		)
		port map(
			clk_in       => sysClk_in,
			
			-- DVR interface -> Connects to comm_fpga module
			chanAddr_in  => chanAddr,
			h2fData_in   => h2fData,
			h2fValid_in  => h2fValid,
			h2fReady_out => h2fReady,
			f2hData_out  => f2hData,
			f2hValid_out => f2hValid,
			f2hReady_in  => f2hReady,
			
			-- Peripheral interface
			spiClk_out   => spiClk_out,
			spiData_out  => spiData_out,
			spiData_in   => spiData_in,
			spiCS_out    => spiCS_out
		);
end architecture;
